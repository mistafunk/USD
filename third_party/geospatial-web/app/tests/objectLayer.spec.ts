// ArcGIS 3D object layers through a wrapper layer (D24): the app reads a
// layer (mocks in testdata/arcgis, the REST API's shape; reference.py
// arcgis-mock), generates a wrapper with the geospatial CRS schemas and loads
// the glb models through the Adobe glTF plugin, all in the browser module.
//   zurich-ellipsoidal  WGS 84 without vertical CRS: placed at the origins
//   zurich-egm96        WGS 84 + EGM96 heights: the worker fetches the EGM96
//                       grid (D23), heights become ellipsoidal (+47.4 m)
//   transform           translation, non-uniform scale, 90 degree rotation
//                       about up: the assumed semantics, R(S p) + T in ENU
// Needs: geo.py build wasm32.
import { expect, test, type Page } from "@playwright/test";
import type { LiveData } from "../src/live";

interface App {
  ready: boolean;
  updating: boolean;
  stats: { frames: number; meshes: number };
  errors: string[];
  live: LiveData | null;
}

async function open(page: Page, view: string, layer: string,
                    extra = ""): Promise<{ app: App; live: LiveData }> {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(`/?view=${view}&data=live&layer=/usd/fixtures/arcgis/${layer}${extra}`);
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app?: App }).__app;
    return (a?.ready && !a.updating && a.stats.frames > 0) || (a?.errors.length ?? 0) > 0;
  }, null, { timeout: 90_000 });
  const app = await page.evaluate(() => (globalThis as unknown as { __app: App }).__app);
  expect(app.errors, "app errors").toEqual([]);
  expect(errors).toEqual([]);
  console.log(`${layer} ${view}: ${JSON.stringify(app.live!.objectLayer?.crs)}, ` +
              `timings ${JSON.stringify(app.live!.timings)}`);
  for (const a of app.live!.anchors) {
    console.log(`  ${a.path} ${a.status}: ${a.status === "ok" ? a.operation : a.reason}`);
  }
  // The panel's load status: finished, the item widget shows the layer, the
  // log names the layer, the model downloads and each anchor.
  await expect(page.locator("#phase")).toHaveText(/^ready: \d+ mesh/);
  await expect(page.locator("#item")).toHaveValue(`/usd/fixtures/arcgis/${layer}`);
  const log = await page.locator("#log").textContent();
  expect(log).toMatch(/spatial reference/);
  expect(log).toMatch(/downloaded \d/);
  expect(log).toMatch(/\/ObjectLayer\/Feature_\d+: ok, 1 mesh/);
  return { app, live: app.live! };
}

/** Column-major 4x4 times a point. */
function apply(m: number[], p: number[]): number[] {
  return [0, 1, 2].map((r) => m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]);
}

const ZURICH = [[8.51294297, 47.39215091, 427.17952808], [8.5120502, 47.39098808, 445.95899328]];

for (const view of ["global", "local"]) {
  test(`object layer zurich-ellipsoidal ${view}`, async ({ page }) => {
    const { app, live } = await open(page, view, "zurich-ellipsoidal");
    expect(live.objectLayer!.skipped).toEqual([]);
    expect(live.objectLayer!.wrapper).toContain("GeospatialCRSBindingAPI");
    expect(live.anchors.map((a) => a.status)).toEqual(["ok", "ok"]);
    expect(app.stats.meshes).toBe(2);
    if (view === "global") {
      // Reference layers, on by default with an object layer; the buildings
      // checkbox hides them without a reload.
      await expect(page.locator("#terrain")).toBeChecked();
      await expect(page.locator("#buildings")).toBeChecked();
      await expect(page.locator("#log")).toContainText(/buildings: ".*" shown/, { timeout: 30_000 });
      await page.locator("#buildings").uncheck();
      expect(page.url()).toContain("buildings=0");
      await page.locator("#buildings").check();
      // EPSG:4326 promoted to 3D into EPSG:4979: the origins unchanged.
      live.anchors.forEach((a, i) => {
        expect(Math.abs(a.origin[0] - ZURICH[i][0])).toBeLessThan(1e-9);
        expect(Math.abs(a.origin[1] - ZURICH[i][1])).toBeLessThan(1e-9);
        expect(Math.abs(a.origin[2] - ZURICH[i][2])).toBeLessThan(1e-6);
      });
    }
    await page.screenshot({ path: `test-results/object-layer-zurich-${view}.png` });
  });
}

test("object layer zurich-egm96 global", async ({ page }) => {
  const { live } = await open(page, "global", "zurich-egm96");
  expect(live.objectLayer!.crs).toBe("EPSG:4326+5773");
  expect(live.grids).toEqual(["us_nga_egm96_15.tif"]);
  // EGM96 height 427.18 m at the first origin is 474.56 m above the ellipsoid
  // (native cs2cs with the same grid).
  expect(live.anchors.map((a) => a.status)).toEqual(["ok", "ok"]);
  expect(Math.abs(live.anchors[0].origin[2] - 474.562305)).toBeLessThan(1e-3);
  live.anchors.forEach((a, i) => {
    expect(Math.abs(a.origin[0] - ZURICH[i][0])).toBeLessThan(1e-9);
    expect(Math.abs(a.origin[1] - ZURICH[i][1])).toBeLessThan(1e-9);
    expect(a.origin[2] - ZURICH[i][2]).toBeGreaterThan(40);
  });
  // Display correction (D25, on with terrain): lowered by the EGM96 geoid
  // height, the models are drawn at the layer's own heights, where the
  // terrain's orthometric heights put the ground.
  expect(live.geoid!.crs).toBe("EPSG:5773");
  live.anchors.forEach((a, i) => {
    const n = live.geoid!.heights[a.path];
    expect(Math.abs(a.origin[2] - n - ZURICH[i][2])).toBeLessThan(1e-3);
  });
  expect(Math.abs(live.geoid!.heights["/ObjectLayer/Feature_1"] - 47.382)).toBeLessThan(1e-3);
});

test("object layer zurich-egm96 global without geoid correction", async ({ page }) => {
  const { live } = await open(page, "global", "zurich-egm96", "&geoid=0");
  expect(live.geoid).toBeUndefined();
  await expect(page.locator("#geoid")).not.toBeChecked();
});

test("object layer transform global", async ({ page }) => {
  const { live } = await open(page, "global", "transform");
  expect(live.anchors.map((a) => a.status)).toEqual(["ok"]);
  const mesh = live.meshes.find((m) => m.status === "ok")!;
  // The glb's corners (x east, y up, z south) mapped by the mesh matrix into
  // metres east/north/up at the anchor, vs R(90 deg about up)(S p) + T.
  const [sx, sy, sz] = [4, 4, 1];   // half east, height, half north of 8 x 4 x 2
  for (const g of [[sx, 0, sz], [-sx, sy, -sz], [sx, sy, -sz]]) {
    const enu = [g[0], -g[2], g[1]];
    const scaled = [2 * enu[0], 1 * enu[1], 1.5 * enu[2]];
    const rotated = [-scaled[1], scaled[0], scaled[2]];
    const expected = [rotated[0] + 10, rotated[1] + 5, rotated[2] + 2];
    const got = apply(mesh.matrix, g);
    for (let k = 0; k < 3; k++) expect(Math.abs(got[k] - expected[k])).toBeLessThan(1e-6);
  }
});

test("layer combobox keeps found layers across loads", async ({ page }) => {
  // As stored by the Find button; loading a layer reloads the page.
  const found = [["https://example.invalid/FeatureServer/0", "Found layer (3 features, EPSG:4326)"]];
  await page.addInitScript((v) => localStorage.setItem("geospatial-web.foundLayers", v),
                           JSON.stringify(found));
  await open(page, "global", "transform");
  await page.locator("#item").fill("/usd/fixtures/arcgis/zurich-ellipsoidal");
  await page.locator("#itemForm button[type=submit]").click();
  await page.waitForURL(/zurich-ellipsoidal/);
  const values = await page.locator("#itemPresets option").evaluateAll(
    (os) => os.map((o) => (o as HTMLOptionElement).value));
  expect(values[0]).toBe(found[0][0]);
  expect(values).toContain("/usd/fixtures/arcgis/transform");
});
