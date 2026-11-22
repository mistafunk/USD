// W2: the harness scores the recorded data (testdata/scene) in the global and
// local views. Every case must produce its expected outcome: pass (A, B and
// the I1 sites F, G, I, J, K), fail (C, a planted 5 cm reference error; I in
// the local view, T5), unsupported (D 2D CRS, E broken binding, H NAVD88). Writes reports/w2-<timestamp>.{json,html} and reports/latest.*.
import { expect, test, type Page } from "@playwright/test";
import type { HarnessReport } from "../src/scene";
import { writeReport } from "./report";

// Recorded data directory: the reference export, or the C++ observer's
// (GEO_RECORDED_SRC=recorded-cpp after geo.py record).
const src = process.env.GEO_RECORDED_SRC ?? "recorded";

test.describe.configure({ mode: "serial" });

interface App {
  ready: boolean;
  updating: boolean;
  stats: { frames: number };
  errors: string[];
  score(): HarnessReport | null;
}

const reports: HarnessReport[] = [];

async function settle(page: Page) {
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app?: App }).__app;
    return (a?.ready && !a.updating && a.stats.frames > 0) || (a?.errors.length ?? 0) > 0;
  }, null, { timeout: 90_000 });
}

for (const mode of ["global", "local"]) {
  test(`w2 ${mode}`, async ({ page }) => {
    await page.goto(`/?view=${mode}&data=recorded&src=${src}`);
    await settle(page);
    const report = await page.evaluate(() =>
      (globalThis as unknown as { __app: App }).__app.score());
    expect(report).not.toBeNull();
    reports.push(report!);
    for (const c of report!.cases) {
      console.log(`${mode}: ${c.anchor} expected ${c.expect}, got ${c.result} (${c.detail})`);
    }
    // Matrix placement: the 1 km runway (Site I, T5) is off by about 2.5 cm
    // at its ends in Web Mercator.
    expect(report!.cases.map((c) => [c.anchor, c.result])).toEqual([
      ["/World/SiteA", "pass"], ["/World/SiteB", "pass"], ["/World/SiteC", "fail"],
      ["/World/SiteD", "pass"], ["/World/SiteE", "unsupported"],
      ["/World/SiteF", "pass"], ["/World/SiteG", "pass"], ["/World/SiteH", "pass"],
      ["/World/SiteI", mode === "local" ? "fail" : "pass"], ["/World/SiteJ", "pass"],
      ["/World/SiteK", "pass"]]);
    expect(report!.summary.unexpected).toBe(0);
    await page.screenshot({ path: `test-results/w2-${mode}.png` });
  });
}

test.afterAll(() => {
  if (reports.length) console.log("report: " + writeReport("reports", reports));
});
