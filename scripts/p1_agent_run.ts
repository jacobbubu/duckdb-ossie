/** Explicit single-scene live CLI. Import, prepare, rehearsal and replay never call this entry. */
import assert from "node:assert/strict";
import { resolve } from "node:path";
import { executeP1 } from "./p1_agent.ts";
import { SCENES, type Scene } from "./p1_agent_contract.ts";
export function p1LiveArguments(args: string[]) {
  assert(
    args.length === 10 &&
      args[0] === "--run" &&
      args[1] === "--token-budget-only" &&
      args[2] === "--scene" &&
      SCENES.includes(args[3] as Scene) &&
      args[4] === "--cli" &&
      args[6] === "--extension" &&
      args[8] === "--out",
    "Usage: p1_agent_run.ts --run --token-budget-only --scene facts|states|history --cli PATH --extension PATH --out NEW_PRIVATE_DIR",
  );
  return {
    mode: "live" as const,
    scene: args[3] as Scene,
    cli: resolve(args[5]),
    artifact: resolve(args[7]),
    out: resolve(args[9]),
  };
}
if (import.meta.main) {
  try {
    const result = await executeP1(p1LiveArguments(process.argv.slice(2)));
    console.log(JSON.stringify(result, null, 2));
    if (result.status !== "complete") process.exitCode = 1;
  } catch {
    console.log(
      JSON.stringify({
        schema: "ossie-p1-live-entry-error-v1",
        status: "failed",
        code: "entry_or_archive_validation_failure",
        details:
          "No arbitrary exception text printed; inspect private sealed records if created",
      }),
    );
    process.exitCode = 1;
  }
}
