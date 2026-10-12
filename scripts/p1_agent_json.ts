/** Bounded linear duplicate-key walk: JSON.parse validates grammar without a giant token regex. */
import assert from "node:assert/strict";
export function p1JSON(raw: string, maxBytes = 1048576): unknown {
  assert(Buffer.byteLength(raw) <= maxBytes, "JSON byte budget exceeded");
  const result: unknown = JSON.parse(raw);
  let i = 0,
    nodes = 0;
  const whitespace = () => {
    while (/\s/.test(raw[i] ?? "") && i < raw.length) i++;
  };
  const string = () => {
    const start = i++;
    while (i < raw.length) {
      const c = raw[i++];
      if (c === "\\") i++;
      else if (c === '"') return raw.slice(start, i);
    }
    throw Error("unterminated string");
  };
  function walk(depth: number) {
    assert(depth <= 64 && ++nodes <= 200000, "JSON structural budget exceeded");
    whitespace();
    if (raw[i] === "{") {
      i++;
      whitespace();
      const keys = new Set<string>();
      if (raw[i] === "}") {
        i++;
        return;
      }
      while (true) {
        whitespace();
        const key = JSON.parse(string()) as string;
        assert(!keys.has(key), "duplicate JSON key");
        keys.add(key);
        whitespace();
        assert(raw[i++] === ":");
        walk(depth + 1);
        whitespace();
        if (raw[i] === "}") {
          i++;
          return;
        }
        assert(raw[i++] === ",");
      }
    }
    if (raw[i] === "[") {
      i++;
      whitespace();
      if (raw[i] === "]") {
        i++;
        return;
      }
      while (true) {
        walk(depth + 1);
        whitespace();
        if (raw[i] === "]") {
          i++;
          return;
        }
        assert(raw[i++] === ",");
      }
    }
    if (raw[i] === '"') {
      string();
      return;
    }
    while (i < raw.length && !/[\s,}\]]/.test(raw[i])) i++;
  }
  walk(0);
  whitespace();
  assert(i === raw.length);
  return result;
}
