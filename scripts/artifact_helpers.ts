/** Independent npm DuckDB client. No build-tree/static fallback. */
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { resolve, join } from "node:path";
import { tmpdir } from "node:os";
import {
  DuckDBInstance,
  DuckDBConnection,
  DuckDBDecimalValue,
  DuckDBDateValue,
  type DuckDBValue,
  listValue,
  VARCHAR,
  LIST,
} from "@duckdb/node-api";
export { assert };
export const ROOT = resolve(import.meta.dir, "..");
export function digest(path: string) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}
export function json<T>(path: string): T {
  return JSON.parse(readFileSync(path, "utf8"));
}
export function sqlString(value: string) {
  return "'" + value.replaceAll("'", "''") + "'";
}
export async function temporary<T>(
  prefix: string,
  action: (directory: string) => Promise<T>,
): Promise<T> {
  const directory = mkdtempSync(join(tmpdir(), prefix));
  try {
    return await action(directory);
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}
export type Cell = string | number | boolean | null;
export function cell(value: DuckDBValue): Cell {
  if (value instanceof DuckDBDecimalValue || value instanceof DuckDBDateValue)
    return value.toString();
  if (typeof value === "bigint") {
    const result = Number(value);
    assert(Number.isSafeInteger(result), "unsafe integer");
    return result;
  }
  assert(
    value === null || ["string", "number", "boolean"].includes(typeof value),
    `unexpected scalar type: ${value}`,
  );
  return value as Cell;
}
export type Parameter = string | string[];
export class Database {
  constructor(
    readonly instance: DuckDBInstance,
    readonly connection: DuckDBConnection,
  ) {}
  static async open(path = ":memory:") {
    const instance = await DuckDBInstance.create(path, {
      allow_unsigned_extensions: "true",
      threads: "1",
    });
    try {
      return new Database(instance, await instance.connect());
    } catch (error) {
      instance.closeSync();
      throw error;
    }
  }
  async rows(sql: string, parameters: Parameter[] = []): Promise<Cell[][]> {
    const result = await this.connection.runAndReadAll(
      sql,
      parameters.map((value) =>
        Array.isArray(value) ? listValue(value) : value,
      ),
      parameters.map((value) =>
        Array.isArray(value) ? LIST(VARCHAR) : VARCHAR,
      ),
    );
    return result.getRows().map((row) => row.map(cell));
  }
  async one(sql: string, parameters: Parameter[] = []): Promise<Cell[]> {
    const result = await this.rows(sql, parameters);
    assert.equal(result.length, 1, sql);
    return result[0];
  }
  async run(sql: string, parameters: Parameter[] = []) {
    await this.rows(sql, parameters);
  }
  async records(sql: string): Promise<Record<string, unknown>[]> {
    const result = await this.connection.runAndReadAll(sql);
    return result.getRowObjectsJson();
  }
  async loadArtifact(artifact: string) {
    const version = await this.one("SELECT * FROM pragma_version()");
    assert.deepEqual(version.slice(0, 2), ["v1.5.6", "069cc9f9b5"]);
    await this.run("LOAD " + sqlString(resolve(artifact)));
    assert.deepEqual(
      await this.one(
        "SELECT loaded FROM duckdb_extensions() WHERE extension_name='ossie'",
      ),
      [true],
    );
    return version;
  }
  close() {
    this.connection.disconnectSync();
    this.instance.closeSync();
  }
}
export function emitReport(report: unknown) {
  const args = process.argv.slice(2);
  assert(
    args.length === 1 || (args.length === 3 && args[1] === "--output"),
    "Usage: bun VERIFIER.ts ARTIFACT [--output RESULT.json]",
  );
  const rendered = JSON.stringify(report, null, 2) + "\n";
  if (args[2]) writeFileSync(resolve(args[2]), rendered);
  process.stdout.write(rendered);
}
