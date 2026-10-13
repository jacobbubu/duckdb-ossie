/** One monotonic elapsed value governs expiry and recording; no wall-clock rescheduling. */
import assert from "node:assert/strict";
export class Deadline {
  private readonly started: number;
  constructor(
    readonly milliseconds: number,
    private readonly now: () => number = () => performance.now(),
  ) {
    assert(Number.isFinite(milliseconds) && milliseconds >= 0);
    this.started = now();
  }
  elapsed() {
    return this.now() - this.started;
  }
  remaining() {
    return Math.max(0, this.milliseconds - this.elapsed());
  }
  assertActive() {
    if (this.elapsed() >= this.milliseconds)
      throw new Error("scenario deadline exceeded");
  }
  onExpiry(fire: () => void) {
    let stopped = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const check = () => {
      if (stopped) return;
      const remaining = this.remaining();
      if (remaining > 0) {
        timer = setTimeout(check, Math.min(remaining, 2147483647));
      } else {
        stopped = true;
        fire();
      }
    };
    timer = setTimeout(check, Math.min(this.remaining(), 2147483647));
    return () => {
      stopped = true;
      clearTimeout(timer);
    };
  }
  async wait<T>(action: () => Promise<T>): Promise<T> {
    this.assertActive();
    let dispose = () => {};
    try {
      const expired = new Promise<never>((_, reject) => {
        dispose = this.onExpiry(() =>
          reject(new Error("scenario deadline exceeded")),
        );
      });
      this.assertActive();
      const result = await Promise.race([action(), expired]);
      this.assertActive();
      return result;
    } finally {
      dispose();
    }
  }
}
export const P1_TIMING = {
  schema: "ossie-p1-timing-v2",
  clock: "performance.now monotonic milliseconds",
  elapsed: "unrounded difference from scene start",
  expiry: "elapsed_ms >= budget_ms",
  scope: "one scene deadline shared by discovery, loop and evaluation",
  hard_realtime: false,
} as const;
