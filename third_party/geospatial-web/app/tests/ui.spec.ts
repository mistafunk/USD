// The panel (D27): tabs, About on demand, the Layers tab's cards from a stored
// search (no portal is queried), Load. Needs: geo.py build wasm32.
import { expect, test } from "@playwright/test";

const discovery = {
  org: "https://zurich.maps.arcgis.com", orgName: "Esri R&D Center Zurich", searchedAt: Date.UTC(2026, 10, 22, 12),
  layers: [
    { url: "https://example.invalid/arcgis/rest/services/Good/FeatureServer/0", title: "Good layer", itemId: "a".repeat(32),
      owner: "someone", modified: Date.UTC(2026, 5, 17), loadable: true, features: 16, crs: "EPSG:4326+5773",
      formats: ["3D_shapebuffer", "3D_glb"] },
    { url: "https://example.invalid/arcgis/rest/services/Urban/FeatureServer/0", title: "Urban model", itemId: "b".repeat(32),
      loadable: false, reason: "Query off (ChangeTracking)", features: 0, crs: "EPSG:4326" },
  ],
};

test("panel tabs and layer cards", async ({ page }) => {
  await page.addInitScript((v) => localStorage.setItem("geospatial-web.discovery.v2", v), JSON.stringify(discovery));
  await page.goto("/?view=global&data=live&layer=/usd/fixtures/arcgis/transform");
  await page.waitForFunction(() => (globalThis as unknown as { __app?: { ready: boolean } }).__app?.ready,
                             null, { timeout: 90_000 });
  // Features is the default tab; About is a dialog, only on demand.
  await expect(page.locator("#tab-features")).toBeVisible();
  await expect(page.locator("#about")).toBeHidden();
  await page.locator("#aboutOpen").click();
  await expect(page.locator("#about")).toBeVisible();
  await expect(page.locator("#about a[href*='OpenUSD-proposals/pull/1']")).toBeVisible();
  await expect(page.locator("#aboutDiagram svg .dg-box.key")).toHaveCount(2);
  await page.keyboard.press("Escape");
  await expect(page.locator("#about")).toBeHidden();

  // Layers: the stored search, rejected layers hidden until "loadable only" is off.
  await page.locator('.tabs button[data-tab="layers"]').click();
  await expect(page.locator("#org")).toHaveValue("zurich.maps.arcgis.com");
  // The org combobox lists all entries from ▾, also with a value in the field;
  // typing filters; picking fills the field.
  await page.locator("#org + .combo-toggle").click();
  await expect(page.locator("#org ~ .combo-menu")).toBeVisible();
  await expect(page.locator("#org ~ .combo-menu .combo-item")).toHaveCount(2);
  await page.locator('#org ~ .combo-menu .combo-item[data-value="arcgis.com"]').click();
  await expect(page.locator("#org")).toHaveValue("arcgis.com");
  await page.locator("#org").fill("");
  await page.locator("#org").pressSequentially("zur");
  await expect(page.locator("#org ~ .combo-menu .combo-item")).toHaveCount(1);
  // ▾ on a filtered list shows all entries again.
  await page.locator("#org + .combo-toggle").click();
  await expect(page.locator("#org ~ .combo-menu .combo-item")).toHaveCount(2);
  await page.locator("#org + .combo-toggle").click();
  await page.locator("#org").fill("");
  await page.locator("#org").pressSequentially("zur");
  await page.keyboard.press("ArrowDown");
  await page.keyboard.press("Enter");
  await expect(page.locator("#org")).toHaveValue("zurich.maps.arcgis.com");
  const cards = page.locator("#layerCards .card");
  await expect(page.locator('#layerCards .card[data-url*="Good"]')).toContainText("16 features");
  await expect(page.locator('#layerCards .card[data-url*="Urban"]')).toHaveCount(0);
  await page.locator("#loadableOnly").uncheck();
  await expect(page.locator('#layerCards .card.rejected[data-url*="Urban"]')).toContainText("Query off");
  // Only search results are listed (test layers are in Development).
  await expect(cards).toHaveCount(2);
  await expect(page.locator('#itemPresets option[value*="Good"]')).toHaveCount(1);
  // Format filter: a glb-only layer is listed under glTF, not under USD.
  await page.locator('#formatFilter button[data-format="usd"]').click();
  await expect(page.locator('#layerCards .card[data-url*="Good"]')).toHaveCount(0);
  await page.locator('#formatFilter button[data-format="gltf"]').click();
  await expect(page.locator('#layerCards .card[data-url*="Good"]')).toHaveCount(1);
  await page.locator('#formatFilter button[data-format="any"]').click();

  // Load (here a test layer from Development): in place, no reload; the URL follows.
  await page.evaluate(() => { (globalThis as unknown as { marker: number }).marker = 1; });
  await page.locator("details.dev > summary").click();
  await page.locator('#devLayers button[data-url*="zurich-ellipsoidal"]').click();
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app: { ready: boolean; loads: number } }).__app;
    return a.ready && a.loads === 2;
  }, null, { timeout: 90_000 });
  expect(page.url()).toContain("layer=%2Fusd%2Ffixtures%2Farcgis%2Fzurich-ellipsoidal");
  expect(await page.evaluate(() => (globalThis as unknown as { marker?: number }).marker)).toBe(1);
  await expect(page.locator("#anchorList .anchor")).toHaveCount(2);
  await expect(page.locator("#phase")).toHaveText(/^ready: 2 mesh/);
  // Solo: only that feature's meshes are drawn; again, all of them.
  const triangles = () => page.evaluate(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles);
  await page.waitForFunction(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles === 24);
  await page.locator('.tabs button[data-tab="features"]').click();
  const solo = page.locator('#anchorList .anchor[data-path="/ObjectLayer/Feature_1"] .solo');
  await solo.click();
  await expect(solo).toHaveClass(/active/);
  await page.waitForFunction(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles === 12);
  await solo.click();
  await page.waitForFunction(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles === 24);
  expect(await triangles()).toBe(24);
  // Mute: that feature is not drawn; unmuted, it is again.
  const mute = page.locator('#anchorList .anchor[data-path="/ObjectLayer/Feature_2"] .mute');
  await mute.click();
  await expect(mute).toHaveClass(/active/);
  await page.waitForFunction(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles === 12);
  await mute.click();
  await page.waitForFunction(() =>
    (globalThis as unknown as { __app: { stats: { triangles: number } } }).__app.stats.triangles === 24);
  // A failed load, then a successful one in place: the status bar is green again.
  await page.evaluate(() => (globalThis as unknown as { __app: { loadLayer(u: string): Promise<void> } })
    .__app.loadLayer("/usd/fixtures/arcgis/does-not-exist"));
  await expect(page.locator("#phase")).toHaveClass(/failed/);
  await page.evaluate(() => (globalThis as unknown as { __app: { loadLayer(u: string): Promise<void> } })
    .__app.loadLayer("/usd/fixtures/arcgis/zurich-ellipsoidal"));
  await expect(page.locator("#phase")).toHaveText(/^ready: 2 mesh/);
  await expect(page.locator("#phase")).not.toHaveClass(/failed/);
  await expect(page.locator("#progressBar")).not.toHaveClass(/failed/);
  // Terrain switches in place too.
  await page.locator("#terrain").uncheck();
  expect(page.url()).toContain("terrain=0");
  expect(await page.evaluate(() => (globalThis as unknown as { marker?: number }).marker)).toBe(1);
});
