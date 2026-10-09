import { test, expect } from "bun:test";
import {
  ERROR_MARKER,
  populationError,
} from "../../scripts/population_error.ts";
test("error framing respects quoted braces, escapes and marker text", () => {
  const data = {
    schema_version: "ossie-population-error-v1" as const,
    code: "INVALID_REQUEST",
    object: '引号"\\\n}',
    stage: "bind",
    reason: ERROR_MARKER + '{"schema_version":"fake"}',
    missing_capability: null,
    exception_type: "Invalid Input",
    source_exception_type: null,
    legacy_message: "object= diagnostic",
  };
  expect(
    populationError(
      "Invalid Input Error: " +
        ERROR_MARKER +
        JSON.stringify(data) +
        "\nLINE 1: SELECT",
    ),
  ).toEqual(data);
});
test("external, incomplete and invalid-schema diagnostics stay unclassified", () => {
  for (const text of [
    "Binder Error: missing signature",
    ERROR_MARKER + "{",
    ERROR_MARKER + '{"schema_version":"fake"}',
    ERROR_MARKER + '{"schema_version":"ossie-population-error-v1"}',
  ])
    expect(populationError(text)).toBeUndefined();
});
