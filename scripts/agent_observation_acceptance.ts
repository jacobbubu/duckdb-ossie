/** Prepare four isolated observation contexts. This driver has no live model mode. */
import assert from "node:assert/strict";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  SCENES,
  OBSERVATION_SYSTEM,
  OBSERVATION_CSV,
  observationInput,
} from "./observation_evidence.ts";
import {
  SERVER,
  runtimeMetadata,
  withObservationSession,
} from "./observation_mcp.ts";
export function preparationArguments(args: string[]) {
  assert(
    args.length === 7 &&
      args[0] === "--prepare" &&
      args[1] === "--cli" &&
      args[3] === "--extension" &&
      args[5] === "--out",
    "Usage: bun scripts/agent_observation_acceptance.ts --prepare --cli PATH --extension PATH --out NEW_DIRECTORY; live mode is not implemented",
  );
  return {
    cli: resolve(args[2]),
    artifact: resolve(args[4]),
    out: resolve(args[6]),
  };
}
export async function prepareObservation(args: string[]) {
  const { cli, artifact, out } = preparationArguments(args);
  assert(!existsSync(out), "fresh experiment directory required");
  mkdirSync(out, { recursive: true, mode: 0o700 });
  const save = (path: string, value: unknown) =>
    writeFileSync(join(out, path), JSON.stringify(value, null, 2) + "\n", {
      flag: "wx",
    });
  const scenes: unknown[] = [];
  try {
    observationInput(); // Hash-bound source gate; oracle values never enter model-context.json.
    const runtime = runtimeMetadata(cli, artifact);
    const git = (args: string[]) => {
      const r = spawnSync("git", args, { cwd: ROOT, encoding: "utf8" });
      assert.equal(r.status, 0);
      return r.stdout.trim();
    };
    save("run.json", {
      mode: "discovery-only",
      model_called: false,
      provider_config_read: false,
      head: git(["rev-parse", "HEAD"]),
      dirty: git(["status", "--porcelain"]),
      runtime,
      hashes: Object.fromEntries(
        [
          SERVER,
          OBSERVATION_CSV,
          ...[
            "agent_observation_acceptance",
            "observation_mcp",
            "observation_evidence",
            "agent_loop",
            "population_result",
          ].map((n) => join(ROOT, `scripts/${n}.ts`)),
        ].map((p) => [p, digest(p)]),
      ),
      scope: "MCP discovery preparation, not Agent acceptance",
    });
    for (const scene of SCENES) {
      const directory = join(out, scene.id);
      mkdirSync(directory);
      await withObservationSession(
        cli,
        artifact,
        scene.threshold,
        async (session) => {
          try {
            const d = await session.discover();
            save(`${scene.id}/setup.json`, {
              scene: scene.id,
              threshold: scene.threshold,
              manifest: d.manifest,
              manifest_sha256: d.manifest_sha256,
              binding: d.binding,
            });
            save(`${scene.id}/model-context.json`, {
              system: OBSERVATION_SYSTEM,
              question: scene.question,
              tools: d.tools,
              resources: d.resources,
            });
            scenes.push({
              scene: scene.id,
              status: "prepared",
              model_called: false,
              binding: d.binding,
            });
          } finally {
            save(`${scene.id}/events.json`, session.events);
          }
        },
      );
    }
    save("summary.json", {
      overall: "discovery-only; not Agent acceptance",
      model_requests: 0,
      scenes,
    });
    return scenes;
  } catch (error) {
    save("failure.json", { error: String(error), model_requests: 0, scenes });
    throw error;
  }
}
if (import.meta.main)
  prepareObservation(process.argv.slice(2))
    .then((scenes) =>
      console.log(
        `${scenes.length} isolated observation preparations passed; no model configuration read or model called`,
      ),
    )
    .catch((error) => {
      console.error(String(error));
      process.exitCode = 1;
    });
