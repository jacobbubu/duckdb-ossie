/** JSON-RPC stdio probe of the actual stock CLI/server.sql, with bounded reads. */
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { ROOT, temporary, sqlString, assert } from "./artifact_helpers.ts";
interface Reply {
  jsonrpc?: string;
  id?: number;
  result?: {
    tools?: { name: string }[];
    resources?: { name?: string; uri?: string }[];
    serverInfo?: { name?: string };
    [key: string]: unknown;
  };
  error?: unknown;
}
export class RpcProcess {
  readonly process: ChildProcessWithoutNullStreams;
  private replies: Reply[] = [];
  private pending?: {
    resolve: (value: Reply) => void;
    reject: (error: Error) => void;
  };
  private ended = false;
  private stderr = "";
  constructor(command: string[]) {
    this.process = spawn(command[0], command.slice(1), {
      stdio: "pipe",
      cwd: ROOT,
    });
    this.process.stderr.on("data", (data) => {
      this.stderr = (this.stderr + String(data)).slice(-4000);
    });
    const lines = createInterface({ input: this.process.stdout });
    lines.on("line", (line) => {
      try {
        const reply: unknown = JSON.parse(line);
        if (!reply || typeof reply !== "object" || !("jsonrpc" in reply))
          return;
        const value = reply as Reply;
        if (this.pending) {
          const pending = this.pending;
          this.pending = undefined;
          pending.resolve(value);
        } else this.replies.push(value);
      } catch {
        /* CLI banners and malformed non-RPC lines are ignored. */
      }
    });
    this.process.on("error", (error) => this.finish(error));
    this.process.on("close", (code) => {
      lines.close();
      this.finish(new Error(`MCP server exited ${code}: ${this.stderr}`));
    });
    // A server that exits during setup must not turn the next write into an unhandled EPIPE.
    this.process.stdin.on("error", (error) => this.finish(error));
  }
  private finish(error: Error) {
    this.ended = true;
    if (this.pending) {
      this.pending.reject(error);
      this.pending = undefined;
    }
  }
  send(message: unknown) {
    if (this.ended) throw new Error(`MCP server ended: ${this.stderr}`);
    this.process.stdin.write(JSON.stringify(message) + "\n");
  }
  async read(timeout = 60000): Promise<Reply> {
    if (this.replies.length) return this.replies.shift()!;
    if (this.ended) throw new Error(`MCP server ended: ${this.stderr}`);
    assert(!this.pending, "overlapping RPC reads");
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
      return await new Promise<Reply>((resolve, reject) => {
        this.pending = { resolve, reject };
        timer = setTimeout(() => {
          this.pending = undefined;
          reject(new Error(`MCP response timeout: ${this.stderr}`));
        }, timeout);
      });
    } finally {
      clearTimeout(timer);
    }
  }
  async request(id: number, method: string, params?: unknown) {
    this.send({
      jsonrpc: "2.0",
      id,
      method,
      ...(params === undefined ? {} : { params }),
    });
    const deadline = Date.now() + 60000;
    while (true) {
      const reply = await this.read(Math.max(1, deadline - Date.now()));
      if (reply.id === id) return reply;
      if (Date.now() >= deadline) throw new Error("MCP request timeout");
    }
  }
  async close() {
    if (this.ended) return;
    const done = new Promise<void>((resolve) =>
      this.process.once("close", () => resolve()),
    );
    this.process.kill("SIGTERM");
    const timer = setTimeout(() => this.process.kill("SIGKILL"), 1000);
    try {
      await done;
    } finally {
      clearTimeout(timer);
    }
  }
}
export async function verify() {
  assert(Bun.which("duckdb"), "duckdb is not on PATH; a stock CLI is required");
  const registryInstall = "INSTALL ossie FROM community;\nLOAD ossie;\n";
  await temporary("ossie-mcp-", async (directory) => {
    let serverPath = join(ROOT, "examples/server.sql"),
      command = ["duckdb"];
    const extension = process.env.OSSIE_EXTENSION;
    if (extension) {
      const text = readFileSync(serverPath, "utf8");
      assert(
        text.includes(registryInstall),
        "server.sql no longer has the exact registry install",
      );
      serverPath = join(directory, "server.sql");
      writeFileSync(
        serverPath,
        text.replace(
          registryInstall,
          `LOAD ${sqlString(resolve(extension))};\n`,
        ),
      );
      command.push("-unsigned");
      console.log(`testing built extension: ${resolve(extension)}`);
    }
    async function withServer(
      command: string[],
      action: (server: RpcProcess) => Promise<void>,
    ) {
      const server = new RpcProcess(command);
      try {
        await action(server);
      } finally {
        await server.close();
      }
    }
    const initialize = {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "ossie-probe", version: "1" },
    };
    function check(label: string, condition: boolean) {
      assert(condition, label);
      console.log(`PASS ${label}`);
    }
    await withServer([...command, "-init", serverPath], async (server) => {
      check(
        "initialize handshake",
        Boolean((await server.request(1, "initialize", initialize)).result),
      );
      server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
      const tools = (await server.request(2, "tools/list")).result?.tools?.map(
        (tool) => tool.name,
      );
      assert.deepEqual(tools, ["semantic_query"]);
      console.log("PASS only semantic_query exposed");
      for (const [index, builtin] of ["query", "export"].entries()) {
        const reply = await server.request(20 + index, "tools/call", {
          name: builtin,
          arguments: { sql: "SELECT 1" },
        });
        check(
          `${builtin} refused`,
          JSON.stringify(reply.error || {}).includes("Tool not found"),
        );
      }
      const resources =
        (await server.request(3, "resources/list")).result?.resources || [];
      check("resources vocabulary", resources.length > 0);
      const body = JSON.stringify(
        (
          await server.request(4, "tools/call", {
            name: "semantic_query",
            arguments: { metrics: "total_sales", dimensions: "item.i_brand" },
          })
        ).result || {},
      );
      check(
        "requested columns and data rows",
        body.includes("item.i_brand") &&
          body.includes("total_sales") &&
          (body.match(/\|/g) || []).length > 6,
      );
      const refusal = await server.request(5, "tools/call", {
        name: "semantic_query",
        arguments: { metrics: "store_productivity" },
      });
      check(
        "grain refusal delivered",
        JSON.stringify(refusal).toLowerCase().includes("grain"),
      );
    });
    const controlPath = join(directory, "control.sql");
    writeFileSync(
      controlPath,
      "INSTALL duckdb_mcp FROM community; LOAD duckdb_mcp;\nPRAGMA mcp_publish_tool('probe_tool','control','SELECT 1 AS x','{}','[]','markdown');\nPRAGMA mcp_server_start('stdio');\n",
    );
    await withServer(["duckdb", "-init", controlPath], async (server) => {
      check(
        "control handshake",
        Boolean((await server.request(1, "initialize", initialize)).result),
      );
      const tools =
        (await server.request(2, "tools/list")).result?.tools?.map(
          (tool) => tool.name,
        ) || [];
      check(
        "without disabling flags query/export exposed",
        tools.includes("query") && tools.includes("export"),
      );
    });
  });
  console.log("all MCP checks passed");
}
if (import.meta.main) {
  try {
    await verify();
  } catch (error) {
    console.error(String(error));
    process.exitCode = 1;
  }
}
