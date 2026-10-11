// Design artifact consistency only. No production import, MCP, model or executor.
import Ajv from "ajv";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";
const directory = import.meta.dir;
const parent = join(directory, "..");
const read = (file: string) => readFileSync(join(directory, file), "utf8");
const hash = (bytes: Buffer) =>
  createHash("sha256").update(bytes).digest("hex");
const check = (condition: unknown, message: string) => {
  if (!condition) throw new Error(message);
};
const validate = new Ajv({ strict: true }).compile(
  JSON.parse(read("request.schema.json")),
);
const examples = JSON.parse(read("request.examples.json"));
for (const request of examples.valid_shape)
  check(validate(request), "valid shape rejected");
for (const request of examples.invalid_shape)
  check(!validate(request), "invalid shape accepted");
const request = (values: unknown[]) => ({
  start_date: "2026-01-31",
  end_date: "2026-02-01",
  products: { mode: "subset", values },
});
check(
  validate(request(Array.from({ length: 64 }, (_, i) => `P${i}`))),
  "64 products",
);
check(
  !validate(request(Array.from({ length: 65 }, (_, i) => `P${i}`))),
  "65 products",
);
// Structural acceptance must NOT be advertised as semantic/resource acceptance.
const semanticOnly = [
  {
    start_date: "2026-02-30",
    end_date: "2026-03-01",
    products: { mode: "all" },
  },
  {
    start_date: "0000-01-01",
    end_date: "0000-01-02",
    products: { mode: "all" },
  },
  {
    start_date: "2026-02-01",
    end_date: "2026-01-31",
    products: { mode: "all" },
  },
  {
    start_date: "2026-01-01",
    end_date: "2027-01-02",
    products: { mode: "all" },
  },
  request(["汉".repeat(86)]),
];
for (const item of semanticOnly)
  check(validate(item), "schema unexpectedly promises semantics");
const allMax = request(
  Array.from(
    { length: 64 },
    (_, i) => `${i.toString().padStart(2, "0")}${"x".repeat(254)}`,
  ),
);
check(validate(allMax), "independent caps shape");
const allMaxBytes = Buffer.byteLength(JSON.stringify(allMax), "utf8");
check(
  allMaxBytes > 16384,
  "64x256 does not demonstrate simultaneous request cap",
);
const old = JSON.parse(readFileSync(join(parent, "verification.json"), "utf8"));
for (const [file, expected] of Object.entries(old.sources_and_outputs_sha256)) {
  check(
    hash(readFileSync(join(parent, file))) === expected,
    `old artifact changed: ${file}`,
  );
}
const decision = JSON.parse(read("acceptance-decision.json"));
for (const [file, expected] of Object.entries(
  decision.accepted_sources_sha256,
)) {
  check(
    hash(readFileSync(join(parent, file))) === expected,
    `accepted source changed: ${file}`,
  );
}
const capability = JSON.parse(read("capability.draft.json"));
check(
  capability.current_api_version === "2.1.0" &&
    capability.daily_user_set.enabled === false,
  "current support overstated",
);
check(
  capability.daily_user_set.supported_concepts.length === 0,
  "draft claims runtime registration",
);
const cases = JSON.parse(readFileSync(join(parent, "cases.json"), "utf8"));
const base = cases.find((c: { id: string }) => c.id === "all-two-days");
check(
  JSON.stringify(base.expected_ids) ===
    JSON.stringify(["B", "E", "F", "H", "K"]),
  "accepted witness mismatch",
);
console.log(
  JSON.stringify(
    {
      scope:
        "Design shape and artifact integrity only; no semantic validator/executor implemented",
      shape_positive_examples: examples.valid_shape.length,
      shape_negative_examples: examples.invalid_shape.length,
      product_cardinality_boundary_shapes: 2,
      deliberately_shape_valid_future_semantic_refusals: semanticOnly.length,
      simultaneous_caps_example: {
        products: 64,
        each_ascii_utf8_bytes: 256,
        request_utf8_bytes: allMaxBytes,
        future_expected: "RESOURCE_LIMIT_EXCEEDED; not executed",
      },
      old_source_output_hashes_unchanged: Object.keys(
        old.sources_and_outputs_sha256,
      ).length,
      accepted_source_hashes_verified: 2,
      runtime_support_enabled: false,
      accepted_witness_members: base.expected_ids,
      production_imports: 0,
      mcp_sessions: 0,
      model_requests: 0,
    },
    null,
    2,
  ),
);
