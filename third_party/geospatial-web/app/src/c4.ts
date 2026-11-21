// C4 browser check: the OpenUSD module in a worker, in a real browser.
//  1. The C1 PROJ placement cases (fixtures/c1, expected values from pyproj):
//     statuses, anchor origins and exact model points within 1 mm.
//  2. The W1/W2 scene through usdImaging, the placement scene index and the
//     observer, in both outputs, against the reference export
//     (public/recorded): statuses, origins, matrices, triangle counts.
// Results go to window.__c4 for tests/c4.spec.ts and to the page.
import { USD_BASE, UsdClient } from "./usd/client";
import type { LiveAnchor, LiveMesh, Vec3 } from "./usd/types";

interface Check {
  group: string;
  name: string;
  ok: boolean;
  detail: string;
}

interface C4State {
  done: boolean;
  error: string | null;
  info: unknown;
  timings: Record<string, number>;
  checks: Check[];
}

const state: C4State = { done: false, error: null, info: null, timings: {}, checks: [] };
(globalThis as unknown as { __c4: C4State }).__c4 = state;

const TOLERANCE_M = 0.001;

function ecef([lon, lat, h]: Vec3): Vec3 {
  const a = 6378137, f = 1 / 298.257223563, e2 = f * (2 - f);
  const l = lon * Math.PI / 180, p = lat * Math.PI / 180;
  const n = a / Math.sqrt(1 - e2 * Math.sin(p) ** 2);
  return [(n + h) * Math.cos(p) * Math.cos(l), (n + h) * Math.cos(p) * Math.sin(l),
          (n * (1 - e2) + h) * Math.sin(p)];
}

const dist = (a: number[], b: number[]) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
const maxDiff = (a: number[], b: number[]) => Math.max(...a.map((v, i) => Math.abs(v - b[i])));
const mm = (m: number) => `${(m * 1000).toFixed(3)} mm`;

function check(group: string, name: string, ok: boolean, detail: string) {
  state.checks.push({ group, name, ok, detail });
}

async function timed<T>(label: string, fn: () => Promise<T>): Promise<T> {
  const t0 = performance.now();
  try {
    return await fn();
  } finally {
    state.timings[label] = Math.round(performance.now() - t0);
  }
}

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return res.json() as Promise<T>;
}

interface C1Expected {
  anchors: {
    stage: string; path: string; status: string; reason?: string;
    origin4979?: Vec3; points?: Vec3[]; points4978?: Vec3[];
  }[];
}

async function c1() {
  const fixtures = `${USD_BASE}/fixtures/c1`;
  const expected = await fetchJson<C1Expected>(`${fixtures}/expected.json`);
  const stages = [...new Set(expected.anchors.map((a) => a.stage))];
  for (const stage of stages) {
    const usd = new UsdClient();
    try {
      await usd.init();
      await usd.load(
        ["crsLibrary.usda", stage].map((f) => ({ url: `${fixtures}/${f}`, path: `/c1/${f}` })),
        `/c1/${stage}`);
      for (const c of expected.anchors.filter((a) => a.stage === stage)) {
        const geo = await usd.resolve(c.path, "EPSG:4979");
        if (c.status !== "ok" || geo.status !== "ok") {
          check("C1", c.path, geo.status === c.status,
                `${geo.status} (expected ${c.status}): ${geo.reason}`);
          continue;
        }
        const geocentric = await usd.resolve(c.path, "EPSG:4978", c.points);
        const origin = dist(ecef(geo.origin!), ecef(c.origin4979!));
        const points = Math.max(...geocentric.outputPoints!.map(
          (q, i) => dist(q, c.points4978![i])));
        check("C1", c.path, origin <= TOLERANCE_M && points <= TOLERANCE_M,
              `ok: origin ${mm(origin)}, ${c.points!.length} points ${mm(points)} ` +
              `(${geo.operation})`);
      }
    } finally {
      usd.terminate();
    }
  }
}

interface Manifest {
  output: { crs: string; frame: string };
  anchors: { path: string; status: string; origin?: Vec3; matrix?: number[] }[];
  meshes: { path: string; status: string; matrix?: number[]; points: { count: number } }[];
}

async function scene(usd: UsdClient, name: string) {
  const ref = await fetchJson<Manifest>(`/recorded/manifest-${name}.json`);
  await timed(`setOutput ${name}`, () => usd.setOutput(ref.output.crs, true));
  const live = await timed(`sync ${name}`, () => usd.state());
  const group = `scene ${name}`;
  check(group, "frame", live.frame === ref.output.frame, `${live.frame}`);

  const anchors = new Map<string, LiveAnchor>(live.anchors.map((a) => [a.path, a]));
  for (const a of ref.anchors) {
    const got = anchors.get(a.path);
    if (!got || got.status !== a.status || a.status !== "ok") {
      check(group, a.path, got?.status === a.status,
            `${got?.status ?? "missing"} (expected ${a.status})${got?.reason ? ": " + got.reason : ""}`);
      continue;
    }
    const origin = ref.output.frame === "enu"
      ? dist(ecef(got.origin), ecef(a.origin!))
      : dist(got.origin, a.origin!);
    const matrix = maxDiff(got.matrix, a.matrix!);
    check(group, a.path, origin <= TOLERANCE_M && matrix < 1e-6,
          `ok: origin ${mm(origin)}, matrix max diff ${matrix.toExponential(1)}`);
  }

  const meshes = new Map<string, LiveMesh>(live.meshes.map((m) => [m.path, m]));
  for (const m of ref.meshes) {
    const got = meshes.get(m.path);
    const counts = got?.points.length === m.points.count * 3;
    if (!got || got.status !== m.status || m.status !== "ok") {
      check(group, m.path, got?.status === m.status && counts,
            `${got?.status ?? "missing"} (expected ${m.status})`);
      continue;
    }
    const matrix = maxDiff(got.matrix, m.matrix!);
    check(group, m.path, counts && matrix < 1e-6,
          `ok: ${got.indices.length / 3} triangles, matrix max diff ${matrix.toExponential(1)}, ` +
          `linearization ${got.linearizationError?.toExponential(1)}`);
  }
}

async function scenes() {
  const fixtures = `${USD_BASE}/fixtures/scene`;
  const index = await fetchJson<{ stage: string; files: string[] }>(`${fixtures}/index.json`);
  const usd = new UsdClient();
  try {
    state.info = await timed("init", () => usd.init());
    await timed("load scene", () => usd.load(
      index.files.map((f) => ({ url: `${fixtures}/${f}`, path: `/scene/${f}` })),
      `/scene/${index.stage}`));
    // The reference evaluates the scene at its geotest:time.
    const test = await usd.testData();
    if (test.time !== null) await usd.setTime(test.time);
    await scene(usd, "global");
    await scene(usd, "local-3857");
  } finally {
    usd.terminate();
  }
}

function render() {
  const failed = state.checks.filter((c) => !c.ok).length;
  const rows = state.checks.map((c) =>
    `<tr class="${c.ok ? "ok" : "bad"}"><td>${c.group}</td><td>${c.name}</td>` +
    `<td>${c.ok ? "ok" : "FAIL"}</td><td>${c.detail}</td></tr>`).join("");
  document.body.innerHTML =
    `<h1>C4 browser module check</h1>` +
    `<p>${state.error ? "Error: " + state.error : `${state.checks.length - failed} ok, ${failed} failed`}` +
    ` &middot; engine ${JSON.stringify(state.info)} &middot; timings ${JSON.stringify(state.timings)}</p>` +
    `<table>${rows}</table>`;
}

(async () => {
  try {
    await timed("C1 cases", c1);
    await timed("scene", scenes);
  } catch (e) {
    state.error = String((e as Error)?.message ?? e);
  }
  state.done = true;
  render();
})();
