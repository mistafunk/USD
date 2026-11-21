// W0 exit checks: cross-origin isolated page, SDK and basemap load under COEP,
// the RenderNode draws in global and local views. Screenshots per camera preset
// go to test-results/ for the visual jitter check (compare with naive float32).
import { expect, test, type Page } from "@playwright/test";

interface W0 {
  ready: boolean;
  updating: boolean;
  crossOriginIsolated: boolean;
  sharedArrayBuffer: boolean;
  stats: { frames: number; meshes: number; triangles: number };
  errors: string[];
  goto(preset: string): Promise<unknown>;
}

async function settle(page: Page) {
  await page.waitForFunction(() => {
    const w0 = (globalThis as unknown as { __w0?: W0 }).__w0;
    return w0?.ready && !w0.updating && w0.stats.frames > 0;
  }, null, { timeout: 90_000 });
  await page.waitForTimeout(500);
}

for (const mode of ["global", "local"]) {
  for (const naive of [false, true]) {
    const suffix = naive ? "-naive" : "";
    test(`${mode}${suffix}`, async ({ page }) => {
      const failed: string[] = [];
      const consoleErrors: string[] = [];
      page.on("requestfailed", (r) => failed.push(`${r.url()} ${r.failure()?.errorText}`));
      page.on("response", (r) => { if (r.status() >= 400) failed.push(`${r.url()} ${r.status()}`); });
      page.on("console", (m) => { if (m.type() === "error") consoleErrors.push(m.text()); });

      await page.goto(`/?view=${mode}${naive ? "&naive=1" : ""}`);
      await settle(page);

      const w0 = await page.evaluate(() => (globalThis as unknown as { __w0: W0 }).__w0);
      expect(w0.crossOriginIsolated).toBe(true);
      expect(w0.sharedArrayBuffer).toBe(true);
      expect(w0.stats.meshes).toBe(3);

      for (const preset of ["overview", "street", "close"]) {
        await page.evaluate((p) => (globalThis as unknown as { __w0: W0 }).__w0.goto(p), preset);
        await settle(page);
        await page.screenshot({ path: `test-results/w0-${mode}${suffix}-${preset}.png` });
      }

      const errors = await page.evaluate(() => (globalThis as unknown as { __w0: W0 }).__w0.errors);
      expect(errors).toEqual([]);
      expect(failed).toEqual([]);
      expect(consoleErrors).toEqual([]);
    });
  }
}
