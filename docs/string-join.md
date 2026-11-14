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
the new batch instead selects API2.5/plan-v3.
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
