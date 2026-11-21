// W1 exit checks: the reference export (two 3DOLs in different CRSs) renders
// in global and local views. Numeric check in render space: Site B's mast,
// placed in geographic 3D, stands on Site A's building corner (UTM 11N).
// Screenshots go to test-results/w1-<view>-<preset>.png.
import { expect, test, type Page } from "@playwright/test";

interface App {
  ready: boolean;
  updating: boolean;
  crossOriginIsolated: boolean;
  stats: { frames: number; meshes: number };
  anchors: { path: string; status: string; detail: string }[];
  errors: string[];
  goto(preset: string): Promise<unknown>;
  renderPoint(mesh: string, p: number[]): number[] | null;
}

const app = (page: Page) => page.evaluate(() => {
  const a = (globalThis as unknown as { __app: App }).__app;
  return { ...a, goto: undefined, renderPoint: undefined };
});

async function settle(page: Page) {
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app?: App }).__app;
    return (a?.ready && !a.updating && a.stats.frames > 0) || (a?.errors.length ?? 0) > 0;
  }, null, { timeout: 90_000 });
  await page.waitForTimeout(500);
}

const renderPoint = (page: Page, mesh: string, p: number[]) =>
  page.evaluate(([m, q]) =>
    (globalThis as unknown as { __app: App }).__app.renderPoint(m as string, q as number[]),
  [mesh, p] as const);

const dist = (a: number[] | null, b: number[] | null) =>
  Math.hypot(a![0] - b![0], a![1] - b![1], a![2] - b![2]);

// testdata/w1: building 40 x 20 x 12 m, mast on its (+x, +y) ground corner.
const BUILDING = "/World/SiteA/Building";
const MAST = "/World/SiteB/Mast";

for (const mode of ["global", "local"]) {
  test(`w1 ${mode}`, async ({ page }) => {
    const failed: string[] = [];
    const consoleErrors: string[] = [];
    page.on("requestfailed", (r) => {
      const why = r.failure()?.errorText ?? "";
      // The SDK cancels pending tile requests when the camera jumps.
      if (why === "net::ERR_ABORTED" && r.url().includes("/tile/")) return;
      failed.push(`${r.url()} ${why}`);
    });
    page.on("response", (r) => { if (r.status() >= 400) failed.push(`${r.url()} ${r.status()}`); });
    page.on("console", (m) => { if (m.type() === "error") consoleErrors.push(m.text()); });

    await page.goto(`/?view=${mode}&data=recorded`);
    await settle(page);

    const a = await app(page);
    expect(a.errors).toEqual([]);
    expect(a.crossOriginIsolated).toBe(true);
    expect(a.anchors.map((x) => [x.path, x.status])).toEqual([
      ["/World/SiteA", "ok"], ["/World/SiteB", "ok"]]);
    expect(a.stats.meshes).toBe(3);

    // Render space: global is the SDK's 6378137 m sphere (east-west about
    // 0.1 % shorter than the ellipsoid at 34 deg N); local is the view SR
    // (Web Mercator units, ~1.21 per metre).
    const corner = await renderPoint(page, BUILDING, [20, 10, 0]);
    const mastBase = await renderPoint(page, MAST, [0, 0, 0]);
    const edge = dist(await renderPoint(page, BUILDING, [-20, -10, 0]),
                      await renderPoint(page, BUILDING, [20, -10, 0]));
    console.log(`${mode}: mast to corner ${dist(corner, mastBase)}, 40 m edge ${edge}`);
    expect(dist(corner, mastBase)).toBeLessThan(0.002);
    if (mode === "global") expect(Math.abs(edge / 40 - 1)).toBeLessThan(0.004);
    else expect(Math.abs(edge / 40 - 1.207)).toBeLessThan(0.005);

    for (const preset of ["overview", "street", "corner"]) {
      await page.evaluate((p) => (globalThis as unknown as { __app: App }).__app.goto(p), preset);
      await settle(page);
      await page.screenshot({ path: `test-results/w1-${mode}-${preset}.png` });
    }

    expect((await app(page)).errors).toEqual([]);
    expect(failed).toEqual([]);
    expect(consoleErrors).toEqual([]);
  });
}
