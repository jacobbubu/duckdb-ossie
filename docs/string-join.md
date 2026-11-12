# Opt-in native String join keys

`native-string-join-v1` extends only `fact-dimension-population-v1`: two
committed native tables, one forward single-column INNER join, `missing=reject`,
and ManyToOne cardinality. It accepts matching native INTEGER/INTEGER or
uncollated VARCHAR/VARCHAR. This is an implementation capability, not a change
to the pinned Ossie `0.2.0.dev0` format or full ontology conformance.

Add the capability to the implementation bundle manifest. It may appear alone or
with `concept-scalar-requires-v1`, in either order; duplicates, empty lists,
unknown capabilities and use on another profile are refused. New opt-in batches
report API `2.4.0`. Default manifests retain the INTEGER-only gate and API `2.2.0`;
sole concept-requires batches retain API `2.3.0`. When explicitly combined with [multiple direct children](direct-derived.md),
the new batch instead selects API2.5/plan-v3; that feature has its own limited Mac ARM64 acceptance.
Otherwise SQL columns and plan-v1 stay the
same. No implicit conversion, additional integer widths, composite keys,
OneToOne opt-in, multiple joins, graph navigation or new profile is added.

VARCHAR equality uses complete UTF-8 bytes, including length. Empty strings are
valid nonnull keys. Embedded NUL bytes are preserved. Case, trailing spaces and
canonically equivalent but differently encoded Unicode strings remain distinct.
There is no trimming, case folding or Unicode normalization. Custom source
collations are refused by capture. Definition JSON strings retain their existing
NUL refusal; the NUL allowance concerns captured native data only.

Every dimension key must be nonnull and unique, including unused rows and when
the fact table is empty. Every fact identity must be nonnull and unique, and
every foreign key must be nonnull and match. Checks precede derived membership
and request filters. Repeated foreign keys are allowed; each fact yields one
frozen row. Valid unused dimensions create no objects. Scalar attribute NULLs
remain subject to the separately enabled concept constraints. Existing capture
limits remain 10,000 rows per input, 256 columns and 16 MiB canonical bytes;
the canonical limit does not bound peak memory.

Discovery adds `typed_join` with schema `ossie-native-string-join-v1`, the actual
bound key type, raw-byte equality, NULL/uniqueness rules and supported/refused
shape. Clients inspect this batch-bound metadata rather than inferring support
from the API version alone. Loading still atomically publishes a frozen snapshot;
failed or interrupted loads retain the previous snapshot, and prepared requests
retain their original collection across reload and source deletion.

## Independent verification

The fact fixture maps its Integer constraint to the fact table, with a conflicting
same-named dimension shadow. The dimension fixture maps its DATE constraint to
the dimension, with a conflicting fact shadow. Both carry repeated FK values,
unused dimensions, full identity-to-label associations, empty/nonempty scopes,
empty/NUL/Unicode/space/case keys and independently calculated root/child groups.
This tests differing mapping dependencies rather than renamed business examples.

`test/tooling/string_join.test.ts` checks fixed schemas, byte pins, the independent
byte-key oracle and rejection of tampered replay/report data without opening a
DB. `string_join_native.test.ts` checks the bounded supervisor wiring. Offline
success does not certify the native extension.

The separate entry point imports without starting processes or opening a DB:

```sh
node scripts/run_string_join_native.ts prepare --output .local-agent-evidence/string-join-prepared-001
node scripts/run_string_join_native.ts build --prepared .local-agent-evidence/string-join-prepared-001/prepared.json --sha256 '<prepared SHA-256>'
node scripts/run_string_join_native.ts run --prepared .local-agent-evidence/string-join-prepared-001/prepared.json --sha256 '<prepared SHA-256>' --build-receipt .local-agent-evidence/string-join-native-build-001/summary.json --build-sha256 '<build summary SHA-256>' --artifact-sha256 '<extension SHA-256>'
```

Prepare freezes source/dependency/tool bytes and commands. Build has a fresh,
fixed `build/string-join-001` target directory; the 14 build outputs are bound
before run. The String worker runs first, then all six original stages, retaining
full original assertions. The default entry point and default quality/CI are
unchanged. Worker limits are 5,000 explicit SQL calls, 120 seconds of work,
150 seconds outer supervision, 8 MiB each for stream/report and zero retries.
The shared run deadline is 1,500 seconds, cleanup 10 seconds and seal 60 seconds;
shared raw output is capped at 64 MiB and archive at 512 MiB.

All output directories must be fresh. Receipts retain paired raw SQL events,
source/artifact pins, independently regenerated derived definitions, complete
answers, expected errors, snapshot/prepared protection and cleanup state. No
failed input/query is automatically corrected. Unknown cleanup remains unknown.
## Limited local acceptance

The frozen candidate-001 passed one controlled build and one native run on Mac
ARM64 with DuckDB 1.5.6 and Bun 1.3.11, without retries. Its two mapping scenarios
passed all 128 empty/nonempty scopes and 3,428 explicit SQL calls: 46 successful
scopes and 82 precisely expected refusals. The independent oracle and hand-written
SQL matched complete identities, values, labels, keys, shadows and groups.
Capture passed 634 checks with threads=4/1, all 31 SQL files passed 1,171 assertions,
and the existing 20-stage regression passed. The original requires boundaries
also passed 408 scopes/9,017 SQL calls and the original A/B passed 144 SQL calls.
This is deterministic local acceptance, with no Agent/model or CLI/MCP run.

The accepted extension SHA-256 is `4d27bac725c815fe70a37fef8dac4ec8b534f3593fd8847a9ea578c443e27d17`.
Prepare, build outputs and raw operation streams remain bound to candidate-001;
the subsequent four-file documentation status update has separate before/after
pins and preserved frozen copies. It does not re-sign the prepared manifest or
make its original strict byte check accept changed documentation. Repeating
acceptance requires fresh reviewed binding and directories, not rewriting receipts.

Old prepared snapshots and public anchors remain tied to their original bytes.
AMD64, cross-platform CI of this unpushed work, public target-tree publication,
broader ontology coverage and the entire P1 are outside this slice.
