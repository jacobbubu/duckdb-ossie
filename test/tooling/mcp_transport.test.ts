import { test, expect } from "bun:test";
import { RpcProcess } from "../../scripts/mcp_check.ts";
import { temporary } from "../../scripts/artifact_helpers.ts";
import { writeFileSync } from "node:fs";
import { join } from "node:path";
test("MCP ignores banners, receives RPC, and cleans up child", async () => {
  await temporary("ossie-rpc-", async (dir) => {
    const script = join(dir, "server.ts");
    writeFileSync(
      script,
      `console.log('banner'); console.log('{broken'); for await (const line of console) {const request=JSON.parse(line);console.log(JSON.stringify({jsonrpc:'2.0',id:request.id,result:{echo:request.method}}));}`,
    );
    const server = new RpcProcess([process.execPath, script]);
    try {
      expect((await server.request(7, "probe")).result?.echo).toBe("probe");
    } finally {
      await server.close();
    }
    expect(
      server.process.exitCode !== null || server.process.signalCode !== null,
    ).toBe(true);
  });
});
test("silent MCP server hits a real read deadline and is terminated", async () => {
  const server = new RpcProcess([
    process.execPath,
    "-e",
    "setInterval(()=>{},1000)",
  ]);
  try {
    await expect(server.read(30)).rejects.toThrow("timeout");
  } finally {
    await server.close();
  }
  expect(server.process.signalCode).not.toBeNull();
});
test("server setup failure reaches reader with stderr", async () => {
  const server = new RpcProcess([
    process.execPath,
    "-e",
    "console.error('setup failed');process.exit(3)",
  ]);
  try {
    await expect(server.read()).rejects.toThrow("setup failed");
  } finally {
    await server.close();
  }
});
