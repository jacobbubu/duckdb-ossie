/** Independent synthetic fixture calculation. Never supplied to a model as an answer. */
import assert from "node:assert/strict";
import { relationDate } from "./relation_result.ts";
import type { FactSetup } from "./fact_mcp.ts";
export type FactObservation = {
  user: string;
  date: string;
  cents: bigint | null;
};
export function factInputs(setup: FactSetup): {
  revenue: FactObservation[];
  cost: FactObservation[];
} {
  assert(
    setup !== "oversize",
    "oversize setup is a refusal probe, not an accepted row oracle",
  );
  if (setup === "empty") return { revenue: [], cost: [] };
  if (setup === "original") {
    const revenue: [string, string, bigint | null][] = [
      ["A", "2026-01-01", 10000n],
      ["A", "2026-01-31", 20000n],
      ["A", "2026-02-01", 5000n],
      ["A", "2026-02-28", 2500n],
      ["B", "2026-01-01", 8000n],
      ["B", "2026-02-01", 0n],
      ["C", "2026-02-01", null],
      ["D", "2026-02-01", 4000n],
    ];
    const cost: [string, string, bigint | null][] = [
      ["A", "2026-01-01", 1000n],
      ["A", "2026-01-01", 2000n],
      ["A", "2026-01-31", 4000n],
      ["A", "2026-02-01", 500n],
      ["A", "2026-02-01", 700n],
      ["A", "2026-02-28", 300n],
      ["B", "2026-01-01", 3000n],
      ["B", "2026-02-01", 0n],
      ["C", "2026-02-01", null],
      ["E", "2026-02-01", 900n],
    ];
    const observations = (values: typeof revenue) =>
      values.map(([user, date, cents]) => ({ user, date, cents }));
    return { revenue: observations(revenue), cost: observations(cost) };
  }
  const users =
    setup === "full"
      ? Array.from(
          { length: 10000 },
          (_, i) => `U${String(i).padStart(5, "0")}`,
        )
      : setup === "raw"
        ? ["é", "a\0", "A", "a"]
        : ["D"];
  const dates =
    setup === "wide"
      ? [
          "5877642-06-25 (BC)",
          "0001-01-01 (BC)",
          "2026-01-01",
          "10000-01-01",
          "5881580-07-10",
        ]
      : ["2026-01-01"];
  const revenue = users.flatMap((user) =>
    dates.map((date) => ({ user, date, cents: 101n })),
  );
  return { revenue, cost: revenue.map((row) => ({ ...row, cents: 2n })) };
}
export function factMoney(cents: bigint | null) {
  if (cents === null) return null;
  const absolute = cents < 0n ? -cents : cents;
  return `${cents < 0n ? "-" : ""}${absolute / 100n}.${String(absolute % 100n).padStart(2, "0")}`;
}
const raw = (a: string, b: string) =>
  Buffer.compare(Buffer.from(a), Buffer.from(b));
export function factUserOracle(
  inputs: ReturnType<typeof factInputs>,
  metrics: string[],
  selected: (values: Record<string, bigint | null>) => boolean = () => true,
) {
  const grouped = (rows: FactObservation[]) => {
    const groups = new Map<string, FactObservation[]>();
    for (const row of rows) {
      const list = groups.get(row.user) ?? [];
      list.push(row);
      groups.set(row.user, list);
    }
    return groups;
  };
  const revenue = grouped(inputs.revenue),
    cost = grouped(inputs.cost);
  const common = [...revenue.keys()].filter((user) => cost.has(user)).sort(raw);
  const sum = (rows: FactObservation[]) => {
    const values = rows.filter((r) => r.cents !== null);
    return values.length ? values.reduce((a, r) => a + r.cents!, 0n) : null;
  };
  const rows = common.flatMap((user) => {
    const r = revenue.get(user)!,
      c = cost.get(user)!,
      income = sum(r),
      expense = sum(c);
    const values: Record<string, bigint | null> = {
      income,
      expense,
      balance: income === null || expense === null ? null : income - expense,
      revenue_rows: BigInt(r.length),
      cost_rows: BigInt(c.length),
      revenue_values: BigInt(r.filter((r) => r.cents !== null).length),
      cost_values: BigInt(c.filter((r) => r.cents !== null).length),
    };
    if (!selected(values)) return [];
    return [
      [
        user,
        ...metrics.map((name) => {
          const k = name.toLowerCase();
          assert(k in values);
          return k.endsWith("rows") || k.endsWith("values")
            ? String(values[k])
            : factMoney(values[k]);
        }),
      ],
    ];
  });
  return {
    rows,
    common_user_count: String(common.length),
    selected_user_count: String(rows.length),
  };
}
export function factWindowOracle(
  inputs: ReturnType<typeof factInputs>,
  windows: string[],
  display: Record<string, unknown>,
) {
  const rows = [...inputs.revenue].sort(
    (a, b) =>
      raw(a.user, b.user) || relationDate(a.date) - relationDate(b.date),
  );
  return rows.flatMap((row, i) => {
    const previous =
      i > 0 && rows[i - 1].user === row.user ? rows[i - 1].cents : null;
    const values = [previous, row.cents].filter((c) => c !== null);
    const rolling = values.length ? values.reduce((a, c) => a + c!, 0n) : null;
    const date = relationDate(row.date);
    if (
      display.mode === "date_range" &&
      (date < relationDate(display.start_date) ||
        date > relationDate(display.end_date))
    )
      return [];
    return [
      [
        row.user,
        row.date,
        ...windows.map((name) =>
          factMoney(name.toLowerCase() === "previous" ? previous : rolling),
        ),
      ],
    ];
  });
}
