"""Offline shape validation of research drafts, not an execution acceptance test."""

import hashlib
import importlib.metadata
import json
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource


ROOT = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest_path = ROOT / "sources/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    schemas = {}
    resources = []
    for relative in ("core-spec/ossie-schema.json", "ontology/ontology.json", "ontology/mapping.json"):
        path = ROOT / "sources/apache-ossie" / relative
        entry = next(item for item in manifest["files"] if item.get("repository") == "apache/ossie"
                     and item["source_path"] == relative)
        if sha(path) != entry["sha256"]:
            raise ValueError(f"Changed schema: {relative}")
        schema = json.loads(path.read_text())
        Draft202012Validator.check_schema(schema)
        schemas[relative] = schema
        resource = Resource.from_contents(schema)
        # Freeze upstream's main-URL cross-schema references without changing source bytes.
        for uri in (schema["$id"], "https://raw.githubusercontent.com/apache/ossie/main/" + relative):
            resources.append((uri, resource))
    registry = Registry().with_resources(resources)  # No network retriever.
    checked = []
    for filename, schema_path in (
        ("gaap.core.json", "core-spec/ossie-schema.json"),
        ("gaap.ontology.threshold-10.json", "ontology/ontology.json"),
        ("gaap.ontology.threshold-20.json", "ontology/ontology.json"),
        ("gaap.mapping.json", "ontology/mapping.json"),
    ):
        path = ROOT / "proposed-definitions" / filename
        document = json.loads(path.read_text())
        Draft202012Validator(schemas[schema_path], registry=registry).validate(document)
        checked.append({"path": filename, "sha256": sha(path), "schema": schema_path})
    mapping = json.loads((ROOT / "proposed-definitions/gaap.mapping.json").read_text())
    core = json.loads((ROOT / "proposed-definitions/gaap.core.json").read_text())
    assert mapping["semantic_model_ref"]["name"] == core["name"]
    ontologies = [json.loads((ROOT / "proposed-definitions" / filename).read_text())
                  for filename in ("gaap.ontology.threshold-10.json", "gaap.ontology.threshold-20.json")]
    assert all(mapping["ontology_ref"]["name"] == item["name"] for item in ontologies)
    # The drafts differ only in one derivation expression, not in metrics/mapping/descriptions.
    ontologies[0]["ontology"][1]["derived_by"] = ["User.ytd_gaap >= 20"]
    assert ontologies[0] == ontologies[1]
    result = {"scope": "Schema and document-name checks only; no parser/compiler/execution validation",
              "jsonschema_version": importlib.metadata.version("jsonschema"),
              "spec_commit": manifest["spec_commit"], "source_manifest_sha256": sha(manifest_path),
              "references": "Pinned resources registered offline; unknown resources fail",
              "documents": checked,
              "definition_change": "Only ValidUser.derived_by changes from >=10 to >=20"}
    (ROOT / "definition-shape-results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
