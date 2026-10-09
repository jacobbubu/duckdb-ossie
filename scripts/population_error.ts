/** Decode a population callback error from a known SQL/MCP failure carrier.
 * This framing parser is not proof of origin; server-side forwarding uses ExtraInfo.
 */
export const ERROR_MARKER = "ossie_population_error_v1: ";
export interface PopulationError {
  schema_version: "ossie-population-error-v1";
  code: string;
  object: string;
  stage: string;
  reason: string;
  missing_capability: string | null;
  exception_type: string;
  source_exception_type: string | null;
  legacy_message: string;
}
export function populationError(message: string): PopulationError | undefined {
  const marker = message.indexOf(ERROR_MARKER);
  if (marker < 0) return;
  const start = marker + ERROR_MARKER.length;
  if (message[start] !== "{") return;
  let depth = 0,
    quoted = false,
    escaped = false;
  for (let i = start; i < message.length; i++) {
    const c = message[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c === "\\") escaped = true;
      else if (c === '"') quoted = false;
    } else if (c === '"') quoted = true;
    else if (c === "{") depth++;
    else if (c === "}" && --depth === 0) {
      try {
        const value = JSON.parse(message.slice(start, i + 1)) as Record<
          string,
          unknown
        >;
        if (value.schema_version !== "ossie-population-error-v1") return;
        if (
          ![
            "code",
            "object",
            "stage",
            "reason",
            "exception_type",
            "legacy_message",
          ].every((k) => typeof value[k] === "string")
        )
          return;
        if (
          !["missing_capability", "source_exception_type"].every(
            (k) => value[k] === null || typeof value[k] === "string",
          )
        )
          return;
        return value as unknown as PopulationError;
      } catch {
        return;
      }
    }
  }
}
