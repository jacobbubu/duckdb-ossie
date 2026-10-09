import { test, expect } from "bun:test";
import {
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  symlinkSync,
  unlinkSync,
  writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import { verifySourceEvidence } from "../../scripts/source_evidence.ts";
const SOURCE = "docs/research/ossie-req-001/sources";
function fixture(
  action: (
    root: string,
    change: (edit: (locator: any) => void) => void,
  ) => void,
) {
  const directory = mkdtempSync(join(tmpdir(), "ossie-portable-source-")),
    root = join(directory, "checkout");
  try {
    mkdirSync(root);
    const verified = verifySourceEvidence();
    for (const path of [
      verified.manifest_path,
      verified.locator_path,
      ...verified.sources.map((s) => s.saved_path),
    ]) {
      mkdirSync(dirname(join(root, path)), { recursive: true });
      copyFileSync(join(ROOT, path), join(root, path));
    }
    action(root, (edit) => {
      const path = join(root, SOURCE, "source-locations.json"),
        locator = JSON.parse(readFileSync(path, "utf8"));
      edit(locator);
      writeFileSync(path, JSON.stringify(locator));
    });
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}
test("fresh checkout verifies 20 sources and the original requirement without external paths", () =>
  fixture((root) => {
    const result = verifySourceEvidence(root);
    expect(result.original_files_checked).toBe(20);
    expect(result.original_requirement_checked).toBe(true);
    expect(result.sources.length).toBe(21);
    expect(result.external_original_paths_read).toBe(false);
  }));
test("missing and single-byte changed source copies refuse", () => {
  fixture((root) => {
    unlinkSync(join(root, SOURCE, "requirements.original.md"));
    expect(() => verifySourceEvidence(root)).toThrow();
  });
  fixture((root) => {
    const p = join(
      root,
      SOURCE,
      "qwenpaw-data/examples/demo_semantic_config.xlsx",
    );
    writeFileSync(p, Buffer.concat([readFileSync(p), Buffer.from([0])]));
    expect(() => verifySourceEvidence(root)).toThrow(
      "immutable source mismatch",
    );
  });
});
test("wrong manifest binding, duplicate, missing and unknown locators refuse", () => {
  for (const [edit, reason] of [
    [
      (l: any) => {
        l.manifest_sha256 = "0".repeat(64);
      },
      "bind the immutable manifest",
    ],
    [
      (l: any) => {
        l.locations.push(l.locations[0]);
      },
      "duplicate source location",
    ],
    [
      (l: any) => {
        l.locations.pop();
      },
      "missing required source location",
    ],
    [
      (l: any) => {
        l.locations[0].original_path = "unknown";
      },
      "unknown or unnecessary",
    ],
  ] as const)
    fixture((root, change) => {
      change(edit);
      expect(() => verifySourceEvidence(root)).toThrow(reason);
    });
});
test("absolute, parent traversal and escaping symlink targets refuse", () => {
  fixture((root, change) => {
    change((l) => {
      l.locations[0].saved_path = "/absolute/raw.xlsx";
    });
    expect(() => verifySourceEvidence(root)).toThrow("repository-relative");
  });
  fixture((root, change) => {
    change((l) => {
      l.locations[0].saved_path = "../raw.xlsx";
    });
    expect(() => verifySourceEvidence(root)).toThrow("parent directories");
  });
  fixture((root) => {
    const file = join(
        root,
        SOURCE,
        "qwenpaw-data/examples/demo_semantic_config.xlsx",
      ),
      outside = join(dirname(root), "outside.xlsx");
    copyFileSync(file, outside);
    unlinkSync(file);
    symlinkSync(outside, file);
    expect(() => verifySourceEvidence(root)).toThrow("outside the checkout");
  });
});
test("locator content hash cannot override the manifest's expected source hash", () =>
  fixture((root, change) => {
    change((l) => {
      l.locations[0].sha256 = "0".repeat(64);
    });
    expect(verifySourceEvidence(root).sources.length).toBe(21);
  }));
