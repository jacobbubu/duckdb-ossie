import { test, expect } from "bun:test";
import {
  readFileSync,
  mkdtempSync,
  mkdirSync,
  copyFileSync,
  rmSync,
  appendFileSync,
} from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import {
  fixedSchemaValidator,
  SCHEMAS,
  type DocumentKind,
} from "../../scripts/check_fixed_schema.ts";
import { ROOT, digest } from "../../scripts/artifact_helpers.ts";
const draft = "test/fixtures/population-observation";
const validate = fixedSchemaValidator();
const document = (kind: DocumentKind) =>
  JSON.parse(readFileSync(join(ROOT, draft, `${kind}.json`), "utf8"));
test("pinned schemas accept standard compound identity sketches without remote resolution", () => {
  const fetch = globalThis.fetch;
  globalThis.fetch = (() => {
    throw new Error("network forbidden");
  }) as unknown as typeof fetch;
  try {
    const offlineValidate = fixedSchemaValidator();
    for (const kind of Object.keys(SCHEMAS) as DocumentKind[])
      expect(offlineValidate(kind, document(kind)).accepted).toBe(true);
  } finally {
    globalThis.fetch = fetch;
  }
});
test("fixed schema rejects wrong version, datatype and malformed nested mapping", () => {
  const core = document("core");
  core.version = "0.1.1";
  expect(validate("core", core).accepted).toBe(false);
  core.version = "0.2.0.dev0";
  core.datasets[0].fields[0].datatype = "NotAType";
  expect(validate("core", core).accepted).toBe(false);
  const mapping = document("mapping");
  mapping.concept_mappings[1].object_mappings[0].referent_mappings[0].referent_mappings = 4;
  expect(validate("mapping", mapping).accepted).toBe(false);
  const ontology = document("ontology");
  ontology.prefixes = { invalid: "not an iri with spaces" };
  expect(validate("ontology", ontology).accepted).toBe(false);
});
test("tampered schema cannot replace a pinned local schema", () => {
  const root = mkdtempSync(join(tmpdir(), "ossie-fixed-schema-"));
  try {
    for (const [relative] of Object.values(SCHEMAS)) {
      const destination = join(
        root,
        "docs/research/ossie-req-001/sources/apache-ossie",
        relative,
      );
      mkdirSync(join(destination, ".."), { recursive: true });
      copyFileSync(
        join(
          ROOT,
          "docs/research/ossie-req-001/sources/apache-ossie",
          relative,
        ),
        destination,
      );
    }
    const path = join(
      root,
      "docs/research/ossie-req-001/sources/apache-ossie",
      SCHEMAS.core[0],
    );
    appendFileSync(path, " ");
    expect(() => fixedSchemaValidator(root)).toThrow("schema hash mismatch");
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
test("both observation fixture bundles are hash-bound and schema accepted", () => {
  for (const threshold of [10, 20]) {
    const manifest = JSON.parse(
      readFileSync(join(ROOT, draft, `bundle-${threshold}.json`), "utf8"),
    );
    for (const kind of Object.keys(SCHEMAS) as DocumentKind[]) {
      const path = join(ROOT, draft, manifest[kind].path);
      expect(digest(path)).toBe(manifest[kind].sha256);
      const data = JSON.parse(readFileSync(path, "utf8"));
      expect(data.name).toBe(manifest[kind].name);
      expect(validate(kind, data).accepted).toBe(true);
    }
  }
});
