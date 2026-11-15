#include "ossie/ontology.hpp"

#include "population_internal.hpp"

namespace duckdb {
namespace ossie {

namespace {

OntologyRelationship ParseRelationship(yyjson_val *value) {
	Keys(value, {"name", "roles", "multiplicity", "description", "ai_context", "verbalizes", "requires", "derived_by"},
	     "relationship");
	OntologyRelationship relationship;
	relationship.name = Text(value, "name");
	for (auto role : Array(value, "roles", true)) {
		Keys(role, {"concept"}, relationship.name);
		relationship.roles.push_back(Text(role, "concept"));
	}
	relationship.multiplicity = Text(value, "multiplicity");
	relationship.verbalizes = Strings(value, "verbalizes");
	relationship.derived_by = Strings(value, "derived_by");
	relationship.
	    requires
	= Strings(value, "requires");
	return relationship;
}

Concept ParseConcept(yyjson_val *value) {
	Keys(value,
	     {"concept", "type", "description", "ai_context", "identify_by", "extends", "derived_by", "requires",
	      "relationships"},
	     "concept");
	Concept concept;
	concept.name = Text(value, "concept");
	concept.type = Text(value, "type");
	concept.identify_by = Strings(value, "identify_by");
	concept.extends = Strings(value, "extends");
	concept.derived_by = Strings(value, "derived_by");
	concept.
	    requires
	= Strings(value, "requires");
	for (auto relationship : Array(value, "relationships")) {
		concept.relationships.push_back(ParseRelationship(relationship));
	}
	return concept;
}

ReferentMapping ParseReferent(yyjson_val *value) {
	Keys(value, {"relationship", "expression", "referent_mappings"}, "referent mapping");
	ReferentMapping referent;
	referent.relationship = Text(value, "relationship");
	auto expression = Member(value, "expression");
	auto nested = Member(value, "referent_mappings");
	if (!expression == !nested) {
		Fail(referent.relationship, "mapping", "referent must be a scalar expression or a nested entity identity");
	}
	if (expression) {
		referent.expression = Text(value, "expression");
		return referent;
	}
	referent.nested = true;
	for (auto scalar : Array(value, "referent_mappings", true)) {
		Keys(scalar, {"relationship", "expression"}, "nested referent mapping");
		referent.referents.push_back({Text(scalar, "relationship"), Text(scalar, "expression")});
	}
	return referent;
}

ObjectMapping ParseObject(yyjson_val *value) {
	Keys(value, {"concept", "expression", "referent_mappings"}, "object mapping");
	ObjectMapping object;
	if (Member(value, "concept")) {
		object.concept = Text(value, "concept");
	}
	auto expression = Member(value, "expression");
	auto referents = Member(value, "referent_mappings");
	if (!expression == !referents) {
		Fail(object.concept.empty() ? "object mapping" : object.concept, "mapping",
		     "object mapping must be a scalar expression or an entity identity");
	}
	if (expression) {
		object.expression = Text(value, "expression");
		return object;
	}
	object.entity = true;
	for (auto referent : Array(value, "referent_mappings", true)) {
		object.referents.push_back(ParseReferent(referent));
	}
	return object;
}

LinkTree ParseLinkTree(yyjson_val *value) {
	Keys(value, {"object_mapping", "children"}, "link mapping");
	LinkTree tree;
	tree.object = ParseObject(Member(value, "object_mapping"));
	for (auto child : Array(value, "children", true)) {
		Keys(child, {"relationship", "object_mapping"}, "link");
		LinkMapping link;
		link.relationship = Text(child, "relationship");
		link.object = ParseObject(Member(child, "object_mapping"));
		tree.children.push_back(std::move(link));
	}
	return tree;
}

ConceptMapping ParseConceptMapping(yyjson_val *value) {
	Keys(value, {"concept", "object_mappings", "link_mappings"}, "concept mapping");
	ConceptMapping mapping;
	mapping.concept = Text(value, "concept");
	for (auto object : Array(value, "object_mappings", true)) {
		mapping.object_mappings.push_back(ParseObject(object));
	}
	for (auto link : Array(value, "link_mappings", true)) {
		mapping.link_mappings.push_back(ParseLinkTree(link));
	}
	return mapping;
}

string ReferenceName(yyjson_val *root, const char *key, const string &object) {
	auto reference = Member(root, key);
	Keys(reference, {"name", "iri"}, object);
	return Text(reference, "name");
}

} // namespace

const OntologyRelationship *Concept::FindRelationship(const string &relationship) const {
	for (auto &candidate : relationships) {
		if (candidate.name == relationship) {
			return &candidate;
		}
	}
	return nullptr;
}

const Concept *Ontology::FindConcept(const string &concept) const {
	for (auto &candidate : concepts) {
		if (candidate.name == concept) {
			return &candidate;
		}
	}
	return nullptr;
}

const ConceptMapping *Mapping::FindConcept(const string &concept) const {
	for (auto &candidate : concept_mappings) {
		if (candidate.concept == concept) {
			return &candidate;
		}
	}
	return nullptr;
}

Ontology ParseOntology(yyjson_val *root) {
	Keys(root, {"version", "name", "description", "ai_context", "ontology", "requires"}, "ontology");
	Version(root);
	Ontology ontology;
	ontology.version = Text(root, "version");
	ontology.name = Text(root, "name");
	ontology.
	    requires
	= Strings(root, "requires");
	for (auto concept : Array(root, "ontology", true)) {
		ontology.concepts.push_back(ParseConcept(concept));
	}
	return ontology;
}

Mapping ParseMapping(yyjson_val *root) {
	Keys(root,
	     {"version", "name", "description", "ontology_ref", "semantic_model_ref", "concept_mappings",
	      "custom_extensions"},
	     "mapping");
	Version(root);
	Mapping mapping;
	mapping.version = Text(root, "version");
	mapping.name = Text(root, "name");
	mapping.ontology_name = ReferenceName(root, "ontology_ref", "ontology reference");
	mapping.model_name = ReferenceName(root, "semantic_model_ref", "core reference");
	for (auto concept : Array(root, "concept_mappings", true)) {
		mapping.concept_mappings.push_back(ParseConceptMapping(concept));
	}
	return mapping;
}

} // namespace ossie
} // namespace duckdb
