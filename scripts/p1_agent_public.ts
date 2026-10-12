/** Only a fixed P1 summary derivative; never an export of raw context, responses or results. */
import assert from "node:assert/strict";
import {
  constants,
  lstatSync,
  openSync,
  closeSync,
  writeFileSync,
  readFileSync,
} from "node:fs";
import { dirname, join, resolve, sep } from "node:path";
import { hashBytes, verifyArchive } from "./observation_agent_archive.ts";
import { replayP1 } from "./p1_agent_replay.ts";
import { CLEANUP_POLICY } from "./p1_agent_safety.ts";
import { SCENES } from "./p1_agent_contract.ts";
import type { JsonObject } from "./agent_loop.ts";
export function verifiedP1Bytes(value: Uint8Array, expectedHash: string) {
  assert(/^[a-f0-9]{64}$/.test(expectedHash));
  assert.equal(
    hashBytes(value),
    expectedHash,
    "source snapshot changed during summary export",
  );
  return Buffer.from(value).toString("utf8");
}
function noSymlinks(path: string) {
  for (let p = resolve(path); ; p = dirname(p)) {
    try {
      assert(!lstatSync(p).isSymbolicLink(), "summary symlink refused");
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
    }
    if (p === dirname(p)) break;
  }
}
export async function exportP1Summary(
  source: string,
  out: string,
  artifactSha: string,
) {
  const before = verifyArchive(source);
  const replay = await replayP1(source, artifactSha);
  assert.equal(
    replay.source_seal_sha256,
    before.seal_sha256,
    "source changed between validation and replay",
  );
  const bytes = (name: string) => {
    const value = readFileSync(join(before.path, name));
    return verifiedP1Bytes(value, before.files[name]);
  };
  const read = (name: string) => JSON.parse(bytes(name)) as JsonObject;
  const run = read("run.json"),
    summary = read("summary.json");
  const scenes = (summary.scenes as JsonObject[]).map((item, index) => {
    const scene = SCENES[index];
    assert(
      item.scene === scene &&
        ["prepared", "mechanical_pass", "task_failed", "failed"].includes(
          String(item.status),
        ),
    );
    const events = bytes(scene + "/events.jsonl")
      .trim()
      .split("\n")
      .filter(Boolean)
      .map((v) => JSON.parse(v) as JsonObject);
    const requests = events.filter((e) => e.kind === "provider_request");
    return {
      scene,
      status: item.status,
      cleanup_failed: !!item.cleanup_failure,
      request_attempts: requests.length,
      tool_calls: events.filter((e) => e.kind === "tool_request").length,
      maximum_outbound_utf8_bytes: requests.reduce(
        (max, e) => Math.max(max, Buffer.byteLength(String(e.serialized_body))),
        0,
      ),
      semantic_acceptance: "not_proven",
    };
  });
  const limits = run.limits as JsonObject;
  const value = {
    schema: "ossie-p1-public-summary-v1",
    derived: true,
    mode: replay.mode,
    status: replay.status,
    correspondence: run.startup_failure
      ? "startup_diagnostic_only"
      : scenes.some((s) => s.status === "failed")
        ? "recorded_diagnostic_or_execution"
        : "recorded_simulation_or_preparation",
    real_model_requests: 0,
    semantic_acceptance: "not_proven",
    source_seal_sha256: before.seal_sha256,
    artifact_sha256: artifactSha,
    source_inventory_sha256: hashBytes(JSON.stringify(run.source_hashes)),
    limits: {
      rounds: limits.rounds,
      calls: limits.calls,
      milliseconds: limits.milliseconds,
      output_tokens: limits.outputTokens,
      outbound_bytes: limits.inputBytes,
      inbound_bytes: run.inbound_bytes,
    },
    cleanup_policy: CLEANUP_POLICY,
    scenes,
    limitations: [
      "Synthetic fixture and scripted responses only",
      "No model autonomy, provider authenticity, real business completeness, token sufficiency or billing proof",
      "Summary is not the original wire or a public replay archive",
    ],
  };
  assert.equal(
    verifyArchive(source).seal_sha256,
    before.seal_sha256,
    "source changed during summary export",
  );
  out = resolve(out);
  assert(
    out !== before.path && !out.startsWith(before.path + sep),
    "summary cannot modify source archive",
  );
  noSymlinks(out);
  const text = JSON.stringify(value, null, 2) + "\n";
  const fd = openSync(
    out,
    constants.O_WRONLY |
      constants.O_CREAT |
      constants.O_EXCL |
      constants.O_NOFOLLOW,
    0o644,
  );
  try {
    writeFileSync(fd, text);
  } finally {
    closeSync(fd);
  }
  return {
    schema: value.schema,
    summary_sha256: hashBytes(text),
    source_seal_sha256: before.seal_sha256,
  };
}
if (import.meta.main) {
  const a = process.argv.slice(2);
  assert(
    a.length === 6 &&
      a[0] === "--source" &&
      a[2] === "--artifact-sha" &&
      a[4] === "--out",
    "Usage: p1_agent_public.ts --source PRIVATE_DIR --artifact-sha SHA256 --out NEW_SUMMARY_FILE",
  );
  console.log(
    JSON.stringify(
      await exportP1Summary(resolve(a[1]), resolve(a[5]), a[3]),
      null,
      2,
    ),
  );
}
