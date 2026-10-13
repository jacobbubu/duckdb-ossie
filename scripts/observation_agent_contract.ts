/** Fixed local experiment contract; no environment or network access. */
import { join, relative } from "node:path";
import { ROOT } from "./artifact_helpers.ts";
import { LIMITS } from "./agent_loop.ts";
import { SERVER } from "./observation_mcp.ts";
import { OBSERVATION_CSV } from "./observation_evidence.ts";
export const AGENT_SOURCE_FILES = [
  SERVER,
  OBSERVATION_CSV,
  ...[
    "observation_agent_contract",
    "observation_agent_run",
    "observation_agent_replay",
    "observation_agent_archive",
    "agent_loop",
    "agent_deadline",
    "observation_mcp",
    "observation_evidence",
    "population_result",
    "population_error",
    "mcp_check",
  ].map((name) => join(ROOT, `scripts/${name}.ts`)),
];
export const SOURCE_NAMES = AGENT_SOURCE_FILES.map((path) =>
  relative(ROOT, path),
);
export const BUDGET = {
  per_scene: LIMITS,
  scenes: 4,
  maximum_model_requests: 32,
  maximum_tool_calls: 48,
  maximum_requested_output_tokens: 65536,
  maximum_input_bytes_per_request: LIMITS.inputBytes,
  monetary_ceiling: "unknown; not enforced",
  input_token_ceiling: "not enforced",
};
