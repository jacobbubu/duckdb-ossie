/** Standard configuration and explicitly selected transport. Importing this file reads no environment. */
import assert from "node:assert/strict";
import { providerBaseURL, type JsonObject } from "./agent_loop.ts";
import { p1ResponseBoundary } from "./p1_agent_safety.ts";
import { abortable } from "./p1_agent_simulation.ts";
export type P1Request = (
  input: string | URL | Request,
  init?: RequestInit,
) => Promise<Response>;
export type P1Config = { baseURL: string; model: string; key: string };
export function p1Configuration(
  env: Record<string, string | undefined>,
): P1Config {
  const baseURL = providerBaseURL(
    env.OPENAI_BASE_URL ?? "https://api.openai.com/v1/",
  );
  const model = env.OPENAI_MODEL?.trim(),
    key = env.OPENAI_API_KEY?.trim();
  assert(model, "OPENAI_MODEL required");
  assert(key, "OPENAI_API_KEY required");
  assert(
    !model.includes(key) && !baseURL.includes(key),
    "credential in configuration refused",
  );
  return { baseURL, model, key };
}
export const PROVIDER_CODES = [
  "configuration_failure",
  "http_failure",
  "transport_failure",
  "response_rejected",
  "credential_echo",
  "cancelled",
] as const;
export type ProviderCode = (typeof PROVIDER_CODES)[number];
export class P1ProviderFailure extends Error {
  constructor(
    readonly code: ProviderCode,
    readonly httpStatus?: number,
  ) {
    super(code);
    assert(PROVIDER_CODES.includes(code), "invalid provider failure code");
    assert(
      httpStatus === undefined ||
        (Number.isInteger(httpStatus) &&
          httpStatus >= 100 &&
          httpStatus <= 599),
      "invalid provider HTTP status",
    );
    this.name = "P1ProviderFailure";
  }
}
export function p1ProtectedFailure(error: unknown, stage: string) {
  return {
    stage,
    category: "Error",
    code:
      error instanceof P1ProviderFailure
        ? error.code
        : error instanceof Error &&
            [
              "scenario deadline exceeded",
              "scene deadline exceeded",
              "This operation was aborted",
            ].includes(error.message)
          ? "cancelled"
          : "validation_failure",
    ...(error instanceof P1ProviderFailure && error.httpStatus !== undefined
      ? { http_status: error.httpStatus }
      : {}),
    reason: "Failure details withheld; inspect private projected evidence",
  };
}
export function credentialEcho(value: unknown, key: string) {
  const text = JSON.stringify(value);
  return (
    !!key &&
    [
      key,
      JSON.stringify(key).slice(1, -1),
      JSON.stringify(JSON.stringify(key).slice(1, -1)).slice(1, -1),
    ].some((v) => text.includes(v))
  );
}
/** Records only the whitelisted derivative or a fixed failure code; never headers/raw bytes/errors. */
export function p1Transport(
  config: P1Config,
  request: P1Request,
  record: (event: JsonObject) => void,
  inbound: number,
): P1Request {
  return async (url, init) => {
    const signal = init!.signal as AbortSignal;
    let phase: "transport" | "response" = "transport";
    try {
      signal.throwIfAborted();
      record({ kind: "transport_attempt" });
      const response = await abortable(
        Promise.resolve(request(url, init)),
        signal,
      );
      phase = "response";
      record({ kind: "transport_response", http_status: response.status });
      if (!response.ok) {
        void response.body?.cancel().catch(() => undefined);
        throw new P1ProviderFailure("http_failure", response.status);
      }
      const projected = await p1ResponseBoundary(response, inbound, signal);
      const body = (await projected.clone().json()) as JsonObject;
      signal.throwIfAborted();
      if (credentialEcho(body, config.key))
        throw new P1ProviderFailure("credential_echo");
      record({
        kind: "provider_projection",
        projection:
          "whitelisted derivative; not original wire; private reasoning omitted",
        body,
      });
      return projected;
    } catch (error) {
      const failure =
        error instanceof P1ProviderFailure
          ? error
          : new P1ProviderFailure(
              signal.aborted
                ? "cancelled"
                : phase === "transport"
                  ? "transport_failure"
                  : "response_rejected",
            );
      record({
        kind: "provider_failure",
        code: failure.code,
        diagnostic_only: true,
        ...(failure.httpStatus !== undefined
          ? { http_status: failure.httpStatus }
          : {}),
      });
      throw failure;
    }
  };
}
