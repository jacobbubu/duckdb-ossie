/** Locate immutable source evidence inside a checkout; expected hashes come only from the manifest. */
import { readFileSync, realpathSync } from "node:fs";
import { isAbsolute, join, relative, resolve } from "node:path";
import { assert, digest, ROOT } from "./artifact_helpers.ts";

const SOURCE = "docs/research/ossie-req-001/sources";
interface Manifest {
  spec_commit: string;
  files: { saved_path?: string; source_path: string; sha256: string }[];
  original_requirement: { path: string; sha256: string };
}
interface Locator {
  schema_version: string;
  manifest_sha256: string;
  locations: { original_path: string; saved_path: string }[];
}
function sourcePath(root: string, path: string) {
  assert(
    !isAbsolute(path) && path.length > 0,
    "source copy must use a repository-relative path",
  );
  assert(
    !path.split(/[\\/]/).includes(".."),
    "source copy must not traverse parent directories",
  );
  const actual = realpathSync(resolve(root, path));
  const inside = relative(realpathSync(root), actual);
  assert(
    inside &&
      inside !== ".." &&
      !inside.startsWith("../") &&
      !isAbsolute(inside),
    "source copy resolves outside the checkout",
  );
  return actual;
}
export function verifySourceEvidence(root = ROOT) {
  const manifestPath = join(SOURCE, "manifest.json"),
    locatorPath = join(SOURCE, "source-locations.json");
  const manifest = JSON.parse(
    readFileSync(join(root, manifestPath), "utf8"),
  ) as Manifest;
  const locator = JSON.parse(
    readFileSync(join(root, locatorPath), "utf8"),
  ) as Locator;
  assert.equal(locator.schema_version, "ossie-source-locations-v1");
  assert.equal(
    locator.manifest_sha256,
    digest(join(root, manifestPath)),
    "source locator must bind the immutable manifest",
  );
  assert(Array.isArray(locator.locations), "source locations must be an array");
  const required = [
    ...manifest.files.filter((f) => !f.saved_path).map((f) => f.source_path),
    manifest.original_requirement.path,
  ];
  const locations = new Map<string, string>();
  for (const location of locator.locations) {
    assert.equal(typeof location.original_path, "string");
    assert.equal(typeof location.saved_path, "string");
    assert(
      required.includes(location.original_path),
      "unknown or unnecessary source location",
    );
    assert(!locations.has(location.original_path), "duplicate source location");
    locations.set(location.original_path, location.saved_path);
  }
  assert.equal(
    locations.size,
    required.length,
    "missing required source location",
  );
  const sources = [
    ...manifest.files.map((f) => ({
      kind: "source",
      original_path: f.source_path,
      saved_path: f.saved_path || locations.get(f.source_path)!,
      sha256: f.sha256,
    })),
    {
      kind: "original_requirement",
      original_path: manifest.original_requirement.path,
      saved_path: locations.get(manifest.original_requirement.path)!,
      sha256: manifest.original_requirement.sha256,
    },
  ];
  for (const source of sources)
    assert.equal(
      digest(sourcePath(root, source.saved_path)),
      source.sha256,
      `immutable source mismatch: ${source.saved_path}`,
    );
  return {
    spec_commit: manifest.spec_commit,
    manifest_path: manifestPath,
    manifest_sha256: digest(join(root, manifestPath)),
    locator_path: locatorPath,
    locator_sha256: digest(join(root, locatorPath)),
    helper_sha256: digest(import.meta.path),
    sources,
    original_files_checked: manifest.files.length,
    original_requirement_checked: true,
    external_original_paths_read: false,
  };
}
