/** Validate documents against immutable, locally registered Ossie schemas. No remote resolver. */
import Ajv2020 from "ajv/dist/2020.js";
import { createHash } from "node:crypto";
import { createRequire } from "node:module";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { assert, ROOT } from "./artifact_helpers.ts";

export const SPEC_COMMIT = "8dd6732da354f22ca71f16d82626a46039ecdcd9";
export const SCHEMAS = {
  core: [
    "core-spec/ossie-schema.json",
    "5b9cf15d31057e2b7363194b1c254a6b669fc412b3f8f402d4efbdcfa25fcc87",
  ],
  ontology: [
    "ontology/ontology.json",
    "52421ba8161385a4d0117eac2a76863d2ef21ef3434a95334b6b1564371fe149",
  ],
  mapping: [
    "ontology/mapping.json",
    "8db74be12cda94e23240cc502bb2ac0580b69a05431fda35ee0c9e8b7e6f096b",
  ],
} as const;
export type DocumentKind = keyof typeof SCHEMAS;
export function fixedSchemaValidator(root = ROOT) {
  const ajv = new Ajv2020({
    strict: false,
    allErrors: true,
    validateFormats: true,
  });
  const addFormats: (instance: Ajv2020) => void = createRequire(
    import.meta.url,
  )("ajv-formats-draft2019");
  addFormats(ajv);
  const aliases: Record<string, string> = {};
  for (const [kind, [relative, hash]] of Object.entries(SCHEMAS)) {
    const path = join(
      root,
      "docs/research/ossie-req-001/sources/apache-ossie",
      relative,
    );
    const bytes = readFileSync(path);
    assert(
      createHash("sha256").update(bytes).digest("hex") === hash,
      `fixed ${kind} schema hash mismatch`,
    );
    const alias = `https://raw.githubusercontent.com/apache/ossie/main/${relative}`;
    aliases[kind] = alias;
    // main in an upstream $ref is a lookup key only, bound to the pinned local bytes.
    ajv.addSchema(JSON.parse(bytes.toString("utf8")), alias);
  }
  const validators = Object.fromEntries(
    Object.entries(aliases).map(([kind, alias]) => {
      const validate = ajv.getSchema(alias);
      assert(validate, `missing locally registered ${kind} schema`);
      return [kind, validate];
    }),
  );
  return (kind: DocumentKind, data: unknown) => {
    const validate = validators[kind];
    assert(validate, "unknown document kind");
    const accepted = validate(data);
    return { accepted, errors: structuredClone(validate.errors ?? []) };
  };
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  const get = (flag: string) => {
    const index = args.indexOf(flag);
    assert(index >= 0 && args[index + 1], `required ${flag}`);
    return resolve(args[index + 1]);
  };
  const validate = fixedSchemaValidator();
  const documents = Object.fromEntries(
    (Object.keys(SCHEMAS) as DocumentKind[]).map((kind) => {
      const path = get(`--${kind}`);
      const bytes = readFileSync(path);
      return [
        kind,
        {
          sha256: createHash("sha256").update(bytes).digest("hex"),
          ...validate(kind, JSON.parse(bytes.toString("utf8"))),
        },
      ];
    }),
  );
  const passed = Object.values(documents).every((d) => d.accepted);
  writeFileSync(
    get("--out"),
    JSON.stringify(
      {
        spec_commit: SPEC_COMMIT,
        schemas: SCHEMAS,
        remote_resolution: false,
        documents,
        passed,
        scope: "JSON Schema acceptance only; not profile binding or execution",
      },
      null,
      2,
    ) + "\n",
    { flag: "wx" },
  );
  assert(passed, "fixed schema document rejection; inspect report");
  console.log(
    "Three documents accepted by fixed offline schemas; execution not established",
  );
}
