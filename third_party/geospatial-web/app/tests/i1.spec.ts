// I1: the app on the live OpenUSD module (worker, PROJ in the browser), the
// whole browser test plan:
//   harness   every case (T1, T2, T3, T4, T7, T8, control points) gives its
//             expected outcome, in the global and the local view, with matrix
//             placement and with exact points (measurement mode)
//   T2, T3    the operation used is named; NAD27 is placed with NADCON
//             after the worker fetched its grids (D23)
//   T5        matrix vs exact point map per mesh (linearization error)
//   T6        screen error of the float32 pipeline 1 m from a control point
//   T9        browser vs native (geo.py record) within 1 mm, and vs the
//             offline reference; I1 exit: within 1 cm on every case both do
//   T11       every result names the engine
// Needs: geo.py build wasm32 (module), geo.py build native-debug and
// geo.py record (native results in public/recorded-cpp).
import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { expect, test, type Page } from "@playwright/test";
import type { HarnessReport } from "../src/harness";
import type { LiveData } from "../src/live";
import type { ScreenError } from "../src/meshPass";
import { writeReport, type ExtraCheck } from "./report";

test.describe.configure({ mode: "serial" });

interface App {
  ready: boolean;
  updating: boolean;
  stats: { frames: number };
  errors: string[];
  live: LiveData | null;
  score(): HarnessReport | null;
  goto(preset: string): Promise<unknown>;
  screenError(mesh: string, p: number[]): ScreenError | null;
}

interface Manifest {
  output: { frame: string };
  anchors: { path: string; status: string; origin?: number[]; matrix?: number[];
             gridsUsed?: string[] }[];
  meshes: { path: string; status: string; matrix?: number[] }[];
}

const reports: HarnessReport[] = [];
const extra: ExtraCheck[] = [];

const NATIVE_TOLERANCE = 0.001;  // T9
const EXIT_TOLERANCE = 0.01;     // I1 exit criterion
const LINEARIZATION_LIMIT = 0.01; // T5: under 1 cm up to 1 km

function manifest(dir: string, name: string): Manifest | null {
  const file = fileURLToPath(new URL(`../public/${dir}/manifest-${name}.json`, import.meta.url));
  return existsSync(file) ? JSON.parse(readFileSync(file, "utf8")) as Manifest : null;
}

function ecef([lon, lat, h]: number[]): number[] {
  const a = 6378137, f = 1 / 298.257223563, e2 = f * (2 - f);
  const l = (lon * Math.PI) / 180, p = (lat * Math.PI) / 180;
  const n = a / Math.sqrt(1 - e2 * Math.sin(p) ** 2);
  return [(n + h) * Math.cos(p) * Math.cos(l), (n + h) * Math.cos(p) * Math.sin(l),
          (n * (1 - e2) + h) * Math.sin(p)];
}

const dist = (a: number[], b: number[]) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
const maxDiff = (a: number[], b: number[]) => Math.max(...a.map((v, i) => Math.abs(v - b[i])));

function check(group: string, name: string, ok: boolean, detail: string) {
  extra.push({ group, name, ok, detail });
  console.log(`${ok ? "ok  " : "FAIL"} ${group} ${name}: ${detail}`);
}

async function settle(page: Page) {
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app?: App }).__app;
    return (a?.ready && !a.updating && a.stats.frames > 0) || (a?.errors.length ?? 0) > 0;
  }, null, { timeout: 90_000 });
}

/**
 * T9: live anchors and meshes against recorded results (native or reference).
 * Anchors whose reference result used a grid the browser lacks (EGM2008 is
 * not served, D23) are not comparable and listed separately.
 */
function compare(label: string, live: LiveData, ref: Manifest, tolerance: number,
                 gridsUsed: Map<string, string[]>) {
  const enu = live.output.frame === "enu";
  const anchors = new Map(live.anchors.map((a) => [a.path, a]));
  let worstOrigin = 0, worstMatrix = 0, compared = 0;
  const statusMismatch: string[] = [];
  const incomparable = new Set<string>();
  for (const a of ref.anchors) {
    const got = anchors.get(a.path);
    if (got && (gridsUsed.get(a.path) ?? []).some((g) => got.missingGrids.includes(g))) {
      incomparable.add(a.path);
      continue;
    }
    if (!got || got.status !== a.status) {
      statusMismatch.push(`${a.path}: ${got?.status ?? "missing"} vs ${a.status}`);
      continue;
    }
    if (a.status !== "ok") continue;
    compared++;
    worstOrigin = Math.max(worstOrigin, enu ? dist(ecef(got.origin), ecef(a.origin!))
                                            : dist(got.origin, a.origin!));
    worstMatrix = Math.max(worstMatrix, maxDiff(got.matrix, a.matrix!));
  }
  const meshes = new Map(live.meshes.map((m) => [m.path, m]));
  for (const m of ref.meshes) {
    const got = meshes.get(m.path);
    if (got && [...incomparable].some((p) => m.path.startsWith(p + "/"))) continue;
    if (!got || got.status !== m.status) {
      statusMismatch.push(`${m.path}: ${got?.status ?? "missing"} vs ${m.status}`);
    } else if (m.status === "ok") {
      worstMatrix = Math.max(worstMatrix, maxDiff(got.matrix, m.matrix!));
    }
  }
  const unit = enu ? "m" : "units";
  if (incomparable.size) {
    check("T9", `${label}: not comparable in the browser`, true,
          [...incomparable].map((p) => `${p} (needs ${gridsUsed.get(p)!.join(", ")})`).join("; "));
  }
  check("T9", `${label}: statuses`, statusMismatch.length === 0,
        statusMismatch.length ? statusMismatch.join("; ") : `all ${ref.anchors.length} anchors, ` +
        `${ref.meshes.length} meshes agree`);
  check("T9", `${label}: ${compared} placed anchors`,
        worstOrigin <= tolerance && worstMatrix < 1e-6,
        `origin max ${(worstOrigin * 1000).toFixed(3)} mm (${unit}/1000), ` +
        `matrix max element diff ${worstMatrix.toExponential(1)}; tolerance ${tolerance * 1000} mm`);
}

const runs = [
  { view: "global", exact: false },
  { view: "local", exact: false },
  { view: "global", exact: true },
  { view: "local", exact: true },
];

for (const run of runs) {
  const label = `${run.view}${run.exact ? " exact" : ""}`;
  test(`i1 ${label}`, async ({ page }) => {
    const pageErrors: string[] = [];
    page.on("pageerror", (e) => pageErrors.push(e.message));
    await page.goto(`/?view=${run.view}&data=live${run.exact ? "&exact=1" : ""}`);
    await settle(page);
    const app = await page.evaluate(() => {
      const a = (globalThis as unknown as { __app: App }).__app;
      return { errors: a.errors, live: a.live };
    });
    expect(app.errors, "app errors").toEqual([]);
    expect(pageErrors).toEqual([]);
    const live = app.live!;
    console.log(`${label}: ${live.output.name} (${live.output.crs}), time ${live.time}, ` +
                `engine ${live.info.engine}, timings ${JSON.stringify(live.timings)}`);

    // Harness: every case as expected (expectLocal applies to local matrix runs).
    const report = (await page.evaluate(() =>
      (globalThis as unknown as { __app: App }).__app.score()))!;
    reports.push(report);
    for (const c of report.cases) {
      console.log(`${label}: ${c.anchor} expected ${c.expect}, got ${c.result} (${c.detail})`);
    }
    expect(report.cases).toHaveLength(11);
    expect(report.summary.unexpected).toBe(0);
    expect(report.mode).toBe(run.exact ? "exact" : "matrix");

    // T11: results name the engine; T2, T3: the operations used.
    const placed = report.cases.filter((c) => c.operation !== undefined);
    expect(placed.length).toBeGreaterThan(0);
    for (const c of placed) expect(c.engine).toBe("proj");
    const siteF = report.cases.find((c) => c.anchor === "/World/SiteF")!;
    const siteG = report.cases.find((c) => c.anchor === "/World/SiteG")!;
    check("T2", `${label}: WGS 72 operation`, /WGS 72 to WGS 84/.test(siteF.operation ?? ""),
          `${siteF.operation} (accuracy ${siteF.accuracy} m)`);
    // T3 (D23): the worker fetches NAD27's grids, then it is placed with
    // NADCON like natively.
    check("T3", `${label}: NAD27 through NADCON, grids fetched on demand`,
          /NAD27 to NAD83 \(1\)/.test(siteG.operation ?? "") &&
          live.grids.includes("us_noaa_conus.tif"),
          `${siteG.operation} (accuracy ${siteG.accuracy} m); grids fetched: ${live.grids.join(", ")}`);

    // T9: browser vs native and vs the reference, same output.
    // CRS descriptions (D27): identifiers found for authored and promoted CRSs.
    const crs = live.crs.anchors;
    check("D27", `${label}: CRS descriptions`,
      crs["/World/SiteD"].defaultHeight === "EPSG:3855" && crs["/World/SiteD"].dimension === 2 &&
      crs["/World/SiteH"].vertical?.id === "EPSG:6360" && crs["/World/SiteH"].horizontal?.id === "EPSG:2229" &&
      crs["/World/SiteA"].horizontal?.id === "EPSG:32611" && crs["/World/SiteE"].error !== "",
      `D ${crs["/World/SiteD"].defaultHeight}, H ${crs["/World/SiteH"].id}, A ${crs["/World/SiteA"].horizontal?.id}`);
    const native = manifest("recorded-cpp", live.output.name);
    expect(native, "native results missing; run: geo.py record").not.toBeNull();
    const reference = manifest("recorded", live.output.name)!;
    const gridsUsed = new Map(reference.anchors.map((a) => [a.path, a.gridsUsed ?? []]));
    console.log(`${label}: grids fetched ${JSON.stringify(live.grids)}`);
    compare(`${label} browser vs native`, live, native!, NATIVE_TOLERANCE, gridsUsed);
    compare(`${label} browser vs reference`, live, reference, EXIT_TOLERANCE, gridsUsed);

    // T5: the matrix shortcut per mesh.
    if (!run.exact) {
      for (const m of live.meshes.filter((x) => x.status === "ok")) {
        const e = m.linearizationError ?? NaN;
        const runwayInMercator = run.view === "local" && m.path.endsWith("/Runway");
        check("T5", `${label}: ${m.path}`, runwayInMercator ? e > LINEARIZATION_LIMIT
                                                            : e < LINEARIZATION_LIMIT,
              `matrix vs exact max ${(e * 1000).toFixed(3)} mm` +
              (runwayInMercator ? " (finding: over 1 cm at 1 km in Web Mercator; exact mode passes)"
                                : ""));
      }
    }

    // T6: 1 m from the building corner where the mast stands.
    if (!run.exact) {
      const frames = await page.evaluate(() =>
        (globalThis as unknown as { __app: App }).__app.stats.frames);
      await page.evaluate(() => (globalThis as unknown as { __app: App }).__app.goto("close"));
      await page.waitForFunction((n) =>
        (globalThis as unknown as { __app: App }).__app.stats.frames > n + 2, frames);
      const points: [string, number[]][] = [["/World/SiteA/Building", [20, 10, 0]],
                                            ["/World/SiteB/Mast", [0, 0, 0]]];
      for (const [mesh, p] of points) {
        const e = await page.evaluate(({ m, q }) =>
          (globalThis as unknown as { __app: App }).__app.screenError(m, q), { m: mesh, q: p });
        expect(e).not.toBeNull();
        check("T6", `${label}: ${mesh} at ${e!.distance.toFixed(2)} m`, e!.pixels < 1,
              `${e!.pixels.toFixed(4)} px between the float32 pipeline and Float64`);
      }
      await page.screenshot({ path: `test-results/i1-${run.view}-close.png` });
    }
    await page.evaluate(() => (globalThis as unknown as { __app: App }).__app.goto("overview"));
    await page.screenshot({ path: `test-results/i1-${label.replace(" ", "-")}.png` });

    expect(extra.filter((c) => !c.ok)).toEqual([]);
  });
}

test.afterAll(() => {
  if (reports.length) console.log("report: " + writeReport("reports", reports, "i1", extra));
});
