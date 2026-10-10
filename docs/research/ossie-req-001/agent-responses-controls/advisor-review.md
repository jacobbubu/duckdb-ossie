# Responses continuation design review

Requested Astra/high; effective backend/effort/usage/billing unknown. Read-only, no provider requests or file edits by Advisor.

Decision: accept the bounded Responses design with explicit continuation/evidence constraints. Actual gateway behavior is unknown.

Constraints applied by Lead:
- Flat function tools with strict:false preserve MCP schemas. No automatic schema normalization or chat fallback.
- Repeat model/instructions/tools/max_output_tokens/store/stream/truncation settings every request.
- Use previous_response_id only from the immediately preceding response in this scene; fresh scene has none. Missing/repeated response IDs fail.
- Use call_id, not output item id; prevalidate the complete unique call batch before executing any call. Mixed prose/calls is not final.
- Completed Responses status is required for normal final/tool handling; incomplete token output, failed/queued/unknown/refusal/empty outcomes fail. Internal finish_reason is a normalized loop label, not a claimed Responses wire field.
- Save exact serialized outgoing bodies and public-field response projections, retaining public output order/index, IDs, call IDs, statuses, message phase, usage and incomplete/error code. Private reasoning items are omitted locally.
- store:true means provider-side persistence, not absence of stored reasoning everywhere. Continuation payload size is not effective model-context size; retained history may affect billing.

Verification: 59 tooling tests/238 expects, typecheck/format pass; three isolated actual MCP preparations pass. Mock controls cover multi-call continuation, fresh scenes, missing/repeated IDs, entire batch rejection, incomplete/refusal and continuation HTTP failure without fallback. No live model or compatibility success is claimed. Budget approval is unchanged and pending.
