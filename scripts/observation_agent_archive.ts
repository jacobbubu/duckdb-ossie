/** Private evidence storage and explicitly derived public exports; never writes credentials. */
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  existsSync,
  lstatSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  writeFileSync,
  appendFileSync,
  openSync,
  closeSync,
  constants,
} from "node:fs";
import { join, resolve, relative, dirname, sep } from "node:path";
import { spawnSync } from "node:child_process";
import { ROOT } from "./artifact_helpers.ts";
export const hashBytes = (bytes: Uint8Array | string) =>
  createHash("sha256").update(bytes).digest("hex");
function noSymlinks(path: string) {
  for (let p = resolve(path); ; p = dirname(p)) {
    try {
      assert(!lstatSync(p).isSymbolicLink(), "symlink evidence paths refused");
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
    }
    if (p === dirname(p)) break;
  }
}
function safeName(name: string) {
  assert(
    name.length > 0 &&
      !name.includes("\\") &&
      !name.startsWith("/") &&
      name.split("/").every((part) => part && part !== "." && part !== ".."),
    "unsafe archive filename",
  );
}
export function privatePath(path: string, checkGit = true) {
  path = resolve(path);
  const base = join(ROOT, ".local-agent-evidence");
  assert(
    path.startsWith(base + sep),
    "raw Agent evidence must stay under ignored .local-agent-evidence",
  );
  noSymlinks(path);
  if (checkGit) {
    assert.equal(
      spawnSync("git", ["check-ignore", "--quiet", "--no-index", path], {
        cwd: ROOT,
      }).status,
      0,
      "private output must be Git-ignored",
    );
    assert.notEqual(
      spawnSync("git", ["ls-files", "--error-unmatch", path], {
        cwd: ROOT,
        stdio: "ignore",
      }).status,
      0,
      "private output must not be tracked",
    );
  }
  return path;
}
function permissions(path: string, directory: boolean) {
  const st = lstatSync(path);
  assert(!st.isSymbolicLink());
  assert(directory ? st.isDirectory() : st.isFile());
  assert.equal(
    st.mode & 0o777,
    directory ? 0o700 : 0o600,
    "private evidence permissions must be 0700/0600",
  );
}
function files(path: string, prefix = ""): string[] {
  permissions(path, true);
  return readdirSync(path)
    .sort()
    .flatMap((name) => {
      const file = join(path, name);
      const part = prefix + name;
      assert(!lstatSync(file).isSymbolicLink(), "archive symlink refused");
      if (lstatSync(file).isDirectory()) return files(file, part + "/");
      permissions(file, false);
      return [part];
    });
}
export function replaceStrings(
  value: unknown,
  replacements: [string, string][],
  depth = 0,
): unknown {
  if (!replacements.length) return value;
  assert(depth < 64, "credential projection nesting too deep");
  if (typeof value === "string") {
    let text = value;
    if (/^[{\["]/.test(text.trim())) {
      try {
        const parsed = JSON.parse(text);
        const cleaned = replaceStrings(parsed, replacements, depth + 1);
        if (JSON.stringify(parsed) !== JSON.stringify(cleaned))
          text = JSON.stringify(cleaned);
      } catch (error) {
        if (!(error instanceof SyntaxError)) throw error;
      }
    }
    for (const [from, to] of replacements) {
      if (!from) continue;
      for (const variant of [
        from,
        JSON.stringify(from).slice(1, -1),
        JSON.stringify(JSON.stringify(from).slice(1, -1)).slice(1, -1),
      ])
        text = text.replaceAll(variant, to);
    }
    return text;
  }
  if (Array.isArray(value))
    return value.map((v) => replaceStrings(v, replacements, depth + 1));
  if (value && typeof value === "object")
    return Object.fromEntries(
      Object.entries(value).map(([k, v]) => [
        String(replaceStrings(k, replacements, depth + 1)),
        replaceStrings(v, replacements, depth + 1),
      ]),
    );
  return value;
}
function exclusiveWrite(file: string, text: string, mode = 0o600) {
  const fd = openSync(
    file,
    constants.O_WRONLY |
      constants.O_CREAT |
      constants.O_EXCL |
      constants.O_NOFOLLOW,
    mode,
  );
  try {
    writeFileSync(fd, text);
  } finally {
    closeSync(fd);
  }
}
export class PrivateArchive {
  readonly path: string;
  private sealed = false;
  private readonly secrets: [string, string][];
  private constructor(path: string, secrets: string[]) {
    this.path = path;
    this.secrets = secrets
      .filter(Boolean)
      .map((key) => [key, "[REDACTED_SECRET]"]);
  }
  static create(path: string, secrets: string[] = []) {
    path = privatePath(path);
    assert(!existsSync(path), "fresh private archive required");
    mkdirSync(dirname(path), { recursive: true, mode: 0o700 });
    noSymlinks(path);
    mkdirSync(path, { mode: 0o700 });
    permissions(path, true);
    return new PrivateArchive(path, secrets);
  }
  save(name: string, value: unknown) {
    assert(!this.sealed, "sealed archive is immutable");
    safeName(name);
    const file = join(this.path, name);
    mkdirSync(dirname(file), { recursive: true, mode: 0o700 });
    noSymlinks(file);
    exclusiveWrite(
      file,
      JSON.stringify(replaceStrings(value, this.secrets), null, 2) + "\n",
    );
  }
  startEvents(scene: string) {
    assert(!this.sealed);
    safeName(scene);
    const file = join(this.path, scene, "events.jsonl");
    mkdirSync(dirname(file), { recursive: true, mode: 0o700 });
    noSymlinks(file);
    exclusiveWrite(file, "");
  }
  record(scene: string, event: unknown) {
    assert(!this.sealed);
    safeName(scene);
    const path = join(this.path, scene, "events.jsonl");
    permissions(path, false);
    const fd = openSync(
      path,
      constants.O_WRONLY | constants.O_APPEND | constants.O_NOFOLLOW,
    );
    try {
      const cleaned = replaceStrings(event, this.secrets);
      const redacted = JSON.stringify(event) !== JSON.stringify(cleaned);
      appendFileSync(
        fd,
        JSON.stringify(
          redacted
            ? {
                ...(cleaned as Record<string, unknown>),
                capture_redacted: true,
              }
            : cleaned,
        ) + "\n",
      );
    } finally {
      closeSync(fd);
    }
  }
  seal() {
    assert(!this.sealed);
    const entries = files(this.path);
    const run = JSON.parse(readFileSync(join(this.path, "run.json"), "utf8"));
    this.save("seal.json", {
      mode: run.mode,
      schema: "ossie-observation-agent-archive-v1",
      files: Object.fromEntries(
        entries.map((name) => [
          name,
          hashBytes(readFileSync(join(this.path, name))),
        ]),
      ),
      limitation:
        "SHA256 proves byte integrity relative to this seal, not provider authenticity or external timestamp",
    });
    this.sealed = true;
    return hashBytes(readFileSync(join(this.path, "seal.json")));
  }
}
export function verifyArchive(path: string) {
  path = privatePath(path, false);
  permissions(path, true);
  const bytes = readFileSync(join(path, "seal.json"));
  const seal = JSON.parse(bytes.toString()) as {
    schema: string;
    mode: string;
    files: Record<string, string>;
  };
  assert.equal(seal.schema, "ossie-observation-agent-archive-v1");
  assert.equal(
    bytes.toString(),
    JSON.stringify(seal, null, 2) + "\n",
    "noncanonical or duplicate seal fields",
  );
  assert(
    seal.files && typeof seal.files === "object" && !Array.isArray(seal.files),
  );
  const names = files(path).filter((name) => name !== "seal.json");
  assert.deepEqual(
    names,
    Object.keys(seal.files).sort(),
    "missing/extra archive files",
  );
  for (const name of names) {
    safeName(name);
    assert(/^[a-f0-9]{64}$/.test(seal.files[name]));
    assert.equal(
      hashBytes(readFileSync(join(path, name))),
      seal.files[name],
      "archive byte hash mismatch",
    );
  }
  const run = JSON.parse(readFileSync(join(path, "run.json"), "utf8"));
  assert.equal(run.mode, seal.mode);
  return {
    path,
    mode: seal.mode,
    seal_sha256: hashBytes(bytes),
    files: seal.files,
  };
}
function publicProjection(
  value: unknown,
  replacements: [string, string][],
  depth = 0,
): unknown {
  assert(depth < 24, "public projection nesting too deep");
  if (typeof value === "string") {
    if (/^[{\["]/.test(value.trim())) {
      try {
        const decoded = JSON.parse(value);
        return JSON.stringify(
          publicProjection(decoded, replacements, depth + 1),
        );
      } catch (error) {
        if (error instanceof SyntaxError) {
          /* Ordinary non-JSON text. */
        } else throw error;
      }
    }
    return replaceStrings(value, replacements);
  }
  if (Array.isArray(value))
    return value.map((v) => publicProjection(v, replacements, depth + 1));
  if (value && typeof value === "object")
    return Object.fromEntries(
      Object.entries(value).map(([k, v]) => [
        String(replaceStrings(k, replacements)),
        publicProjection(v, replacements, depth + 1),
      ]),
    );
  return value;
}
/** Write a fresh derivative tree. The original archive is never edited. */
export function exportObservationArchive(source: string, out: string) {
  const verified = verifyArchive(source);
  out = resolve(out);
  noSymlinks(out);
  assert(!existsSync(out), "fresh public derivative directory required");
  assert(
    !out.startsWith(verified.path + sep),
    "public derivative cannot live inside raw archive",
  );
  const run = JSON.parse(readFileSync(join(verified.path, "run.json"), "utf8"));
  assert.equal(run.schema, "ossie-observation-agent-run-v1");
  assert(
    typeof run.provider?.baseURL === "string" &&
      typeof run.provider?.model === "string",
    "provider metadata required for export",
  );
  const replacements: [string, string][] = [
    [run.provider.baseURL, "https://provider.invalid/v1/"],
    [run.provider.model, "[MODEL_REDACTED]"],
  ];
  const provider = new URL(run.provider.baseURL);
  replacements.push(
    [provider.origin, "https://provider.invalid"],
    [provider.hostname, "provider.invalid"],
  );
  const content = new Map<string, string>();
  const derivatives: Record<
    string,
    { raw_sha256: string; derived_sha256: string }
  > = {};
  for (const [name, raw_sha256] of Object.entries(verified.files)) {
    assert(
      /^(run|summary|failure)\.json$/.test(name) ||
        /^(baseline|changed-rule|interval-users|unsupported-every-day)\/(events\.jsonl|setup\.json|model-context\.json|review\.json|failure\.json)$/.test(
          name,
        ),
      "only known experiment evidence can be exported",
    );
    const raw = readFileSync(join(verified.path, name), "utf8");
    const values = name.endsWith(".jsonl")
      ? raw
          .trim()
          .split("\n")
          .filter(Boolean)
          .map((line) => JSON.parse(line))
      : [JSON.parse(raw)];
    const cleaned = values.map((value) =>
      publicProjection(value, replacements),
    );
    const text = name.endsWith(".jsonl")
      ? cleaned.map((v) => JSON.stringify(v)).join("\n") + "\n"
      : JSON.stringify(cleaned[0], null, 2) + "\n";
    for (const [marker] of replacements)
      if (marker)
        assert(
          !text.includes(marker) &&
            !text.includes(JSON.stringify(marker).slice(1, -1)),
          "private provider marker remains in derivative",
        );
    content.set(name, text);
    derivatives[name] = { raw_sha256, derived_sha256: hashBytes(text) };
  }
  content.set(
    "derivative.json",
    JSON.stringify(
      {
        schema: "ossie-observation-agent-derivative-v1",
        source_seal_sha256: verified.seal_sha256,
        mode: run.mode,
        derived: true,
        exact_original_request_bytes: false,
        source_integrity:
          "verify against separate private raw archive; public rewritten serialized_body is not the original wire",
        files: derivatives,
      },
      null,
      2,
    ) + "\n",
  );
  mkdirSync(dirname(out), { recursive: true });
  noSymlinks(out);
  mkdirSync(out);
  for (const [name, text] of content) {
    const file = join(out, name);
    mkdirSync(dirname(file), { recursive: true });
    noSymlinks(file);
    exclusiveWrite(file, text, 0o644);
  }
  verifyArchive(source);
  return {
    files: Object.keys(derivatives).length,
    source_seal_sha256: verified.seal_sha256,
  };
}

/** A replay report must never be added to the sealed source tree. */
export function writeReplayReport(source: string, out: string, value: unknown) {
  const verified = verifyArchive(source);
  out = privatePath(out);
  assert(
    out !== verified.path && !out.startsWith(verified.path + sep),
    "replay output cannot modify sealed archive",
  );
  assert(!existsSync(out), "fresh replay report required");
  noSymlinks(out);
  exclusiveWrite(out, JSON.stringify(value, null, 2) + "\n");
  verifyArchive(source);
}
