import { test, expect, spyOn } from "bun:test";
import { Deadline } from "../../scripts/agent_deadline.ts";
import {
  agentLoop,
  beforeDeadline,
  type Completion,
} from "../../scripts/agent_loop.ts";
import { cleanupSteps } from "../../scripts/p1_agent_safety.ts";
function clock() {
  let now = 0,
    id = 0;
  const pending = new Map<number, { fire: () => void; delay: number }>();
  const time = spyOn(performance, "now").mockImplementation(() => now);
  const timers = spyOn(globalThis, "setTimeout").mockImplementation(((
    fire: () => void,
    delay: number,
  ) => {
    const n = ++id;
    pending.set(n, { fire, delay });
    return n;
  }) as typeof setTimeout);
  const clear = spyOn(globalThis, "clearTimeout").mockImplementation((n) => {
    pending.delete(Number(n));
  });
  return {
    pending,
    set: (n: number) => {
      now = n;
    },
    fire: () => {
      const [n, t] = [...pending.entries()][0];
      pending.delete(n);
      t.fire();
    },
    restore: () => {
      time.mockRestore();
      timers.mockRestore();
      clear.mockRestore();
    },
  };
}
const drain = async () => {
  for (let i = 0; i < 20; i++) await Promise.resolve();
};
const completion: Completion = {
  id: "r",
  message: { role: "assistant", content: "ok", tool_calls: [] },
  finish_reason: "stop",
};
const options = {
  model: "synthetic",
  system: "s",
  question: "q",
  resources: [],
  tools: [],
  limits: {
    rounds: 1,
    calls: 1,
    milliseconds: 10,
    outputTokens: 1,
    inputBytes: 4096,
  },
  record: () => {},
  complete: async () => completion,
  call: async () => ({}),
};
test("expiry and recorded elapsed share the fractional arithmetic boundary", () => {
  const c = clock();
  try {
    c.set(6.001);
    const d = new Deadline(250);
    let fired = false;
    const dispose = d.onExpiry(() => {
      fired = true;
    });
    c.set(256.001);
    expect(d.elapsed()).toBeLessThan(250);
    c.fire();
    expect(fired).toBe(false);
    expect([...c.pending.values()][0].delay).toBeGreaterThan(0);
    c.set(256.002);
    c.fire();
    expect(fired).toBe(true);
    expect(d.elapsed()).toBeGreaterThanOrEqual(250);
    dispose();
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
test("request setup failure cannot leave an armed loop timer or invoke provider", async () => {
  const c = clock();
  let calls = 0;
  try {
    const circular: Record<string, unknown> = {};
    circular.self = circular;
    await expect(
      agentLoop({
        ...options,
        resources: [circular],
        complete: async () => {
          calls++;
          return completion;
        },
      }),
    ).rejects.toThrow(/cyclic|circular/i);
    expect(calls).toBe(0);
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
test("repeated early callbacks keep the original monotonic budget and delayed callbacks expire", () => {
  const c = clock();
  try {
    const d = new Deadline(10);
    let fired = 0;
    const dispose = d.onExpiry(() => fired++);
    c.set(9);
    c.fire();
    c.fire();
    expect(fired).toBe(0);
    expect([...c.pending.values()][0].delay).toBe(1);
    c.set(12);
    c.fire();
    expect(fired).toBe(1);
    dispose();
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
test("zero budget starts neither work nor provider and too-large borrowed deadlines refuse", async () => {
  let calls = 0;
  const action = async () => {
    calls++;
    return 1;
  };
  await expect(new Deadline(0).wait(action)).rejects.toThrow("deadline");
  await expect(
    agentLoop({
      ...options,
      limits: { ...options.limits, milliseconds: 0 },
      complete: async () => {
        calls++;
        return completion;
      },
    }),
  ).rejects.toThrow("deadline");
  await expect(
    agentLoop({ ...options, deadline: new Deadline(11) }),
  ).rejects.toThrow("deadline exceeds");
  expect(calls).toBe(0);
});
test("elapsed setup and request recording cannot borrow time or invoke provider", async () => {
  const c = clock();
  try {
    const d = new Deadline(10);
    c.set(10);
    let calls = 0;
    await expect(
      d.wait(async () => {
        calls++;
        return 1;
      }),
    ).rejects.toThrow("deadline");
    c.set(20);
    const loop = agentLoop({
      ...options,
      record: () => c.set(30),
      complete: async () => {
        calls++;
        return completion;
      },
    });
    await expect(loop).rejects.toThrow("deadline");
    expect(calls).toBe(0);
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
test("late promise fulfillment is refused before its timeout callback can run", async () => {
  const c = clock();
  try {
    let resolve!: (n: number) => void;
    const d = new Deadline(10);
    const result = d.wait(
      () =>
        new Promise<number>((r) => {
          resolve = r;
        }),
    );
    c.set(10);
    resolve(1);
    await expect(result).rejects.toThrow("deadline");
    expect(c.pending.size).toBe(0);
    c.set(20);
    await expect(
      agentLoop({
        ...options,
        complete: async () => {
          c.set(30);
          return completion;
        },
      }),
    ).rejects.toThrow("deadline");
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
test("inner success disposes its timer without disarming the borrowed outer deadline", async () => {
  const c = clock();
  try {
    const d = new Deadline(10);
    let fired = false;
    const outer = d.onExpiry(() => {
      fired = true;
    });
    await agentLoop({ ...options, deadline: d });
    expect(c.pending.size).toBe(1);
    c.set(10);
    c.fire();
    expect(fired).toBe(true);
    outer();
    expect(c.pending.size).toBe(0);
  } finally {
    c.restore();
  }
});
for (const expireAt of ["request", "result"]) {
  test(`expiry at tool ${expireAt} admits no later callback or late result`, async () => {
    const c = clock();
    let calls = 0;
    try {
      const tool = {
        name: "query",
        inputSchema: {
          type: "object",
          properties: { request: { type: "string" } },
          required: ["request"],
        },
      };
      const answer: Completion = {
        id: "r",
        finish_reason: "tool_calls",
        message: {
          role: "assistant",
          content: null,
          tool_calls: ["a", "b"].map((id) => ({
            id,
            type: "function",
            function: { name: "query", arguments: '{"request":"x"}' },
          })),
        },
      };
      await expect(
        agentLoop({
          ...options,
          tools: [tool],
          limits: { ...options.limits, calls: 2 },
          complete: async () => answer,
          record: (e) => {
            if (expireAt === "request" && e.kind === "tool_request") c.set(10);
          },
          call: async () => {
            calls++;
            c.set(10);
            return {};
          },
        }),
      ).rejects.toThrow("deadline");
      expect(calls).toBe(expireAt === "request" ? 0 : 1);
      expect(c.pending.size).toBe(0);
    } finally {
      c.restore();
    }
  });
}
test("rejected action and successful action dispose latest and stale callbacks", async () => {
  for (const fail of [false, true]) {
    const c = clock();
    try {
      let release!: () => void;
      const d = new Deadline(10);
      const result = d.wait(
        () =>
          new Promise<number>((resolve, reject) => {
            release = () =>
              fail ? reject(new Error("original failure")) : resolve(1);
          }),
      );
      c.set(9);
      c.fire();
      const stale = [...c.pending.values()][0].fire;
      release();
      if (fail) await expect(result).rejects.toThrow("original failure");
      else expect(await result).toBe(1);
      expect(c.pending.size).toBe(0);
      c.set(10);
      stale();
      expect(c.pending.size).toBe(0);
    } finally {
      c.restore();
    }
  }
});
test("wall clock jumps after epoch adaptation cannot extend or shorten monotonic waits", async () => {
  const c = clock();
  let wall = 1000;
  const date = spyOn(Date, "now").mockImplementation(() => wall);
  try {
    let settled = false;
    const result = beforeDeadline(
      1010,
      async () => new Promise(() => {}),
    ).catch((e) => {
      settled = true;
      throw e;
    });
    wall = -100000;
    c.set(9);
    c.fire();
    await drain();
    expect(settled).toBe(false);
    wall = 100000;
    c.set(10);
    c.fire();
    await expect(result).rejects.toThrow("deadline");
    expect(c.pending.size).toBe(0);
  } finally {
    date.mockRestore();
    c.restore();
  }
});
for (const failIndex of [0, 1, 2])
  for (const failure of ["reject", "timeout"]) {
    test(`cleanup ${failure} at step ${failIndex} stops later work and disposes timers`, async () => {
      const c = clock();
      const started: number[] = [];
      try {
        const steps = [0, 1, 2].map((i) => async () => {
          started.push(i);
          if (i === failIndex) {
            if (failure === "reject") throw Error("close failed");
            return new Promise(() => {});
          }
          return 1;
        });
        const result = cleanupSteps(steps, 5);
        await drain();
        if (failure === "timeout") {
          c.set(5);
          c.fire();
        }
        await expect(result).rejects.toThrow(
          failure === "timeout" ? "deadline" : "close failed",
        );
        expect(started).toEqual([0, 1, 2].slice(0, failIndex + 1));
        expect(c.pending.size).toBe(0);
      } finally {
        c.restore();
      }
    });
  }
test("cleanup gives each sequential step a new budget despite wall-clock changes", async () => {
  const c = clock();
  const date = spyOn(Date, "now").mockImplementation(() => -1e12);
  try {
    const started: number[] = [];
    let release!: () => void;
    const result = cleanupSteps(
      [
        () =>
          new Promise<void>((r) => {
            started.push(0);
            release = r;
          }),
        () =>
          new Promise<void>((r) => {
            started.push(1);
            release = r;
          }),
      ],
      5,
    );
    c.set(4);
    release();
    await drain();
    expect(started).toEqual([0, 1]);
    expect([...c.pending.values()][0].delay).toBe(5);
    c.set(8);
    release();
    await result;
    expect(c.pending.size).toBe(0);
  } finally {
    date.mockRestore();
    c.restore();
  }
});
