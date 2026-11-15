#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/vector.hpp"

namespace duckdb_yyjson {
struct yyjson_val;
}

namespace duckdb {
namespace ossie {

//! One binary relationship declared on a concept. The role concepts are either value types
//! (String, Date, Decimal, Integer, Float, Boolean) or other concepts in the same ontology.
struct OntologyRelationship {
	string name;
	vector<string> roles;
	string multiplicity;
	vector<string> verbalizes;
	vector<string> derived_by;
	vector<string> requires_list;
};

struct Concept {
	string name;
	string type;
	vector<string> identify_by;
	vector<string> extends;
	vector<string> derived_by;
	vector<string> requires_list;
	vector<OntologyRelationship> relationships;

	const OntologyRelationship *FindRelationship(const string &relationship) const;
};

struct Ontology {
	string name;
	string version;
	vector<string> requires_list;
	vector<Concept> concepts;

	const Concept *FindConcept(const string &concept) const;
};

//! A referent names one identity relationship of the object being mapped. Either it is bound to a
//! scalar expression, or it carries the identity of a referenced entity as nested scalar referents.
struct ScalarReferent {
	string relationship;
	string expression;
};

struct ReferentMapping {
	string relationship;
	string expression;
	bool nested = false;
	vector<ScalarReferent> referents;
};

//! An object mapping is either a scalar value (expression) or an entity (referent_mappings).
struct ObjectMapping {
	string concept;
	string expression;
	bool entity = false;
	vector<ReferentMapping> referents;
};

struct LinkMapping {
	string relationship;
	ObjectMapping object;
};

struct LinkTree {
	ObjectMapping object;
	vector<LinkMapping> children;
};

struct ConceptMapping {
	string concept;
	vector<ObjectMapping> object_mappings;
	vector<LinkTree> link_mappings;
};

struct Mapping {
	string name;
	string version;
	string ontology_name;
	string model_name;
	vector<ConceptMapping> concept_mappings;

	const ConceptMapping *FindConcept(const string &concept) const;
};

//! Parse the standard documents. Only structure is checked here: allowed properties, required
//! strings and arrays, the supported version, and that every object mapping is a scalar or an
//! entity but not both. What a profile accepts is decided by its validator over the result.
Ontology ParseOntology(duckdb_yyjson::yyjson_val *root);
Mapping ParseMapping(duckdb_yyjson::yyjson_val *root);

} // namespace ossie
} // namespace duckdb
