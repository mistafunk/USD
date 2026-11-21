// C4: the OpenUSD module (install/wasm32, served at /usd) runs in a browser
// worker. Exit criterion: the C1 cases give the same results as native PROJ
// (within 1 mm of the pyproj expectations, which native matches at 0 mm), and
// the grid cases are reported unsupported. Also checks the live scene chain
// against the reference export. Needs: geo.py build wasm32.
import { expect, test } from "@playwright/test";

interface C4 {
  done: boolean;
  error: string | null;
  info: unknown;
  timings: Record<string, number>;
  checks: { group: string; name: string; ok: boolean; detail: string }[];
}

test("c4 module", async ({ page, request }) => {
  const module = await request.head("/usd/module/geospatialModule.js");
  expect(module.ok(), "OpenUSD module not installed; run: geo.py build wasm32").toBe(true);
  // Served precompressed (D17).
  const wasm = await request.head("/usd/module/geospatialModule.wasm",
                                  { headers: { "Accept-Encoding": "gzip" } });
  expect(wasm.headers()["content-encoding"]).toBe("gzip");
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/c4.html");
  await page.waitForFunction(
    () => (globalThis as unknown as { __c4?: C4 }).__c4?.done, null, { timeout: 110_000 });
  const c4 = await page.evaluate(() => (globalThis as unknown as { __c4: C4 }).__c4);
  console.log(`engine ${JSON.stringify(c4.info)}, timings ${JSON.stringify(c4.timings)}`);
  for (const c of c4.checks) {
    console.log(`${c.ok ? "ok  " : "FAIL"} ${c.group} ${c.name}: ${c.detail}`);
  }
  await page.screenshot({ path: "test-results/c4.png", fullPage: true });
  expect(c4.error).toBeNull();
  expect(errors).toEqual([]);
  expect(c4.checks.filter((c) => c.group === "C1")).toHaveLength(5);
  expect(c4.checks.filter((c) => !c.ok)).toEqual([]);
});
