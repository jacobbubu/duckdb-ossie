# Bounded implementation review

Requested Advisor Astra/high. Effective backend/effort/usage/billing unknown. Read-only review; no edits, children or provider calls.

Decision: accept the offline milestone; no oracle supplied to the model and no automatic overall acceptance. Before live execution, resolve budget prerequisite and repair configuration/deadline/status issues.

Findings retained:
- Unvalidated configured URL was written to run.json before later rejection; credential-bearing URL could enter evidence.
- Ambient gateway/model variables could override the user's selected destination/model.
- 120-second model timer excluded startup/discovery despite the original total-duration contract.
- Non-stop final responses and pending-review summary of numerical failures could mislead consumers.
- Matching values in cited rows is partial numerical support; row/scope correspondence still needs human review.
- Request/output bounds do not establish a monetary cap when proxy pricing is unknown.

Lead repairs and evidence:
- Resolve credential-free HTTPS configuration and reject endpoint/model mismatches before output creation; local rejection tests pass.
- Share the per-scene 120-second deadline across setup/discovery and model/tool loop; record timestamp/elapsed_ms for events. Expired setup cannot start a request; hung setup/provider controls pass.
- Require final finish_reason=stop; filtered/unknown responses fail.
- Any numerical gate failure sets authoritative experiment summary to fail.
- Rename partial cited-value support and retain mandatory manual semantic review.
- --run additionally requires explicit --token-budget-only choice. This is not a monetary ceiling; actual user budget choice and live compatibility remain pending.

Final local evidence: 54 tests, 184 expectations; typecheck and format pass. Three real MCP preparations pass. No real model request occurred. Tests do not establish gateway compatibility or Agent behavior.
