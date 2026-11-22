// ArcGIS 3D object layers through a wrapper layer (D24): the app reads a
// layer (mocks in testdata/arcgis, the REST API's shape; reference.py
// arcgis-mock), generates a wrapper with the geospatial CRS schemas and loads
// the glb models through the Adobe glTF plugin, all in the browser module.
//   zurich-ellipsoidal  WGS 84 without vertical CRS: z in the scene's height
//                       model (D28), placed at the origins
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
      // No vertical CRS: z is in the scene's height model (D28), with
      // terrain EGM96 like the output, so the anchors are at the layer's z
      // values, as the Maps SDK draws them.
      live.anchors.forEach((a, i) => {
        expect(Math.abs(a.origin[0] - ZURICH[i][0])).toBeLessThan(1e-9);
        expect(Math.abs(a.origin[1] - ZURICH[i][1])).toBeLessThan(1e-9);
        expect(Math.abs(a.origin[2] - ZURICH[i][2])).toBeLessThan(1e-3);
      });
      await expect(page.locator("#log")).toContainText(
        "the layer names no vertical CRS: z in the scene's gravity-related heights (EPSG:5773)");
    }
    expect(live.output.crs).toBe(view === "global" ? "EPSG:4326+5773" : "EPSG:3857+5773");
    await page.screenshot({ path: `test-results/object-layer-zurich-${view}.png` });
  });
}

test("object layer zurich-egm96 global", async ({ page }) => {
  const { live } = await open(page, "global", "zurich-egm96");
  expect(live.objectLayer!.crs).toBe("EPSG:4326+5773");
  expect(live.grids).toEqual(["us_nga_egm96_15.tif"]);
  // With terrain the output has EGM96 heights too (D28): placed through the
  // ellipsoid and back, the anchors are at the layer's own heights.
  expect(live.output.crs).toBe("EPSG:4326+5773");
  expect(live.output.frame).toBe("enu");
  // Read from the view at runtime: gravity-related, no geoid named.
  await expect(page.locator("#log")).toContainText("height model: gravity-related heights without a named geoid: EGM96 assumed");
  await expect(page.locator("#log")).toContainText("height model: gravity (EPSG:5773)");
  expect(live.anchors.map((a) => a.status)).toEqual(["ok", "ok"]);
  live.anchors.forEach((a, i) => {
    expect(Math.abs(a.origin[0] - ZURICH[i][0])).toBeLessThan(1e-9);
    expect(Math.abs(a.origin[1] - ZURICH[i][1])).toBeLessThan(1e-9);
    expect(Math.abs(a.origin[2] - ZURICH[i][2])).toBeLessThan(1e-3);
  });
  // Features tab (D27): the module's CRS description, one row per anchor.
  const crs = live.crs.anchors["/ObjectLayer/Feature_1"];
  expect([crs.type, crs.id, crs.horizontal?.id, crs.vertical?.id])
    .toEqual(["compound", "EPSG:9707", "EPSG:4326", "EPSG:5773"]);
  await expect(page.locator("#anchorList .anchor")).toHaveCount(live.anchors.length);
  await expect(page.locator("#outSummary")).toHaveText(/\(EPSG:4326\+5773\)$/);
  // Defined by the app (an EPSG code, so no WKT shown).
  await expect(page.locator("#outBody")).toContainText("compounddefined bythe app, from the global view (WGS 84) and the scene's height model (terrain: EGM96 heights)");
  await expect(page.locator("#outBody details")).toHaveCount(0);
  await expect(page.locator("#outHeights")).toHaveText(/EGM96/);
  // Input CRS, like the output: the layer's compound, from its spatial reference.
  await expect(page.locator("#inSummary")).toHaveText("WGS 84 + EGM96 height (EPSG:4326+5773)");
  // Selecting and zooming to an anchor: no errors, the camera moves.
  await page.evaluate(() => (globalThis as unknown as { __app: { select(p: string): void } })
    .__app.select("/ObjectLayer/Feature_2"));
  await expect(page.locator('#anchorList .anchor.selected')).toHaveAttribute("data-path", "/ObjectLayer/Feature_2");
  await page.locator('#anchorList .anchor[data-path="/ObjectLayer/Feature_2"] .zoom').click();
  await page.waitForTimeout(1500);
  const errors = await page.evaluate(() => (globalThis as unknown as { __app: App }).__app.errors);
  expect(errors).toEqual([]);
});

test("object layer zurich-egm96 global, terrain switched off", async ({ page }) => {
  await open(page, "global", "zurich-egm96");
  // Without terrain the height model is ellipsoidal: the content is placed
  // again in place (no page reload) in EPSG:4979, where the EGM96 height
  // 427.18 m at the first origin is 474.56 m (native cs2cs, same grid).
  await page.evaluate(() => { (globalThis as unknown as { marker: number }).marker = 1; });
  await page.locator("#terrain").uncheck();
  await page.waitForFunction(() => {
    const a = (globalThis as unknown as { __app: { ready: boolean; loads: number } }).__app;
    return a.ready && a.loads === 2;
  }, null, { timeout: 90_000 });
  const live = await page.evaluate(() => (globalThis as unknown as { __app: { live: LiveData } }).__app.live);
  expect(live.output.crs).toBe("EPSG:4979");
  expect(Math.abs(live.anchors[0].origin[2] - 474.562305)).toBeLessThan(1e-3);
  expect(page.url()).toContain("terrain=0");
  expect(await page.evaluate(() => (globalThis as unknown as { marker?: number }).marker)).toBe(1);
  await expect(page.locator("#outHeights")).toHaveText("ellipsoidal");
});

test("object layer zurich-usd global", async ({ page }) => {
  // A layer that serves USD (usdz) next to glb, as layers are expected to:
  // the app references the usdz models, read natively by OpenUSD.
  const { app, live } = await open(page, "global", "zurich-usd");
  expect(live.objectLayer!.wrapper).toMatch(/references = @\.\/assets\/[0-9a-f]+\.usdz@/);
  expect(live.objectLayer!.wrapper).not.toContain(".glb@");
  expect(live.anchors.map((a) => a.status)).toEqual(["ok", "ok"]);
  expect(app.stats.meshes).toBe(2);
  expect(live.meshes.map((m) => m.triangles)).toEqual([12, 12]);
  live.anchors.forEach((a, i) => {
    expect(Math.abs(a.origin[0] - ZURICH[i][0])).toBeLessThan(1e-9);
    expect(Math.abs(a.origin[1] - ZURICH[i][1])).toBeLessThan(1e-9);
  });
  await expect(page.locator("#log")).toContainText("models as 3D_usdz");
});

test("object layer zurich-duplicate global", async ({ page }) => {
  // Feature 3 repeats feature 1 exactly, after it: reported as a duplicate,
  // and a selected feature 1 is drawn last, so its highlight shows.
  // Without terrain and buildings, so the view's centre is the box.
  const { live } = await open(page, "global", "zurich-duplicate", "&terrain=0&buildings=0");
  expect(live.objectLayer!.duplicates).toEqual({ "/ObjectLayer/Feature_3": "/ObjectLayer/Feature_1" });
  await expect(page.locator('#anchorList .anchor[data-path="/ObjectLayer/Feature_3"] .dup'))
    .toHaveText("duplicate of Feature_1");
  await expect(page.locator("#log")).toContainText("exact duplicate of /ObjectLayer/Feature_1");
  const app = (f: string, ...args: unknown[]) => page.evaluate(([f, args]) =>
    (globalThis as unknown as Record<string, Record<string, (...a: unknown[]) => unknown>>)
      .__app[f as string](...(args as unknown[])), [f, args] as [string, unknown[]]);
  await app("zoom", "/ObjectLayer/Feature_1");
  await page.waitForTimeout(1500);
  const centre = async () => {
    const png = (await page.screenshot()).toString("base64");
    return page.evaluate(async (data) => {
      const img = new Image();
      img.src = `data:image/png;base64,${data}`;
      await img.decode();
      const c = document.createElement("canvas");
      c.width = img.width; c.height = img.height;
      const g = c.getContext("2d")!;
      g.drawImage(img, 0, 0);
      // The zoomed anchor is in the middle of the view, left of the panel.
      return Array.from(g.getImageData(430, 420, 1, 1).data);
    }, png);
  };
  const plain = await centre();
  await app("select", "/ObjectLayer/Feature_1");
  await page.waitForTimeout(500);
  const selected = await centre();
  // The highlight tints towards orange: more red than blue, unlike the plain grey.
  expect(plain[0] - plain[2]).toBeLessThan(10);
  expect(selected[0] - selected[2]).toBeGreaterThan(40);
});

test("object layer transform global", async ({ page }) => {
  // Without terrain: an ENU frame at the ellipsoidal height, so the feature
  // transform is checked alone (with EGM96 heights the frame sits at the
  // drawn height, 7 um per metre different from it).
  const { live } = await open(page, "global", "transform", "&terrain=0&buildings=0");
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
  // As stored by the Find button, read at start-up.
  const found = [["https://example.invalid/FeatureServer/0", "Found layer (3 features, EPSG:4326)"]];
  await page.addInitScript((v) => localStorage.setItem("geospatial-web.foundLayers", v),
                           JSON.stringify(found));
  await open(page, "global", "transform");
  await page.locator('.tabs button[data-tab="layers"]').click();
  await page.locator("#item").fill("/usd/fixtures/arcgis/zurich-ellipsoidal");
  await page.locator("#itemForm button[type=submit]").click();
  await page.waitForURL(/zurich-ellipsoidal/);
  const values = await page.locator("#itemPresets option").evaluateAll(
    (os) => os.map((o) => (o as HTMLOptionElement).value));
  expect(values[0]).toBe(found[0][0]);
  expect(values).toContain("/usd/fixtures/arcgis/transform");
});

// Close to the meshes the app fixes the clip distances (10 cm near, 50 km
// far); once the camera rests far away they are the SDK's automatic ones
// again, in both views (switching views reloads the page).
for (const view of ["global", "local"]) {
  test(`clip distances follow the camera (${view})`, async ({ page }) => {
    await page.goto(`/?view=${view}&data=live&layer=/usd/fixtures/arcgis/zurich-egm96&buildings=0`);
    await page.waitForFunction(() => (globalThis as any).__app?.ready, null, { timeout: 90_000 });
    const clip = () => page.evaluate(() => (globalThis as any).__app.clip());
    expect((await clip()).mode).toBe("auto");
    await page.evaluate(() => (globalThis as any).__app.zoom("/ObjectLayer/Feature_1"));
    await expect.poll(clip).toEqual({ mode: "manual", near: 0.1, far: 50_000 });
    await page.evaluate(() => (globalThis as any).__app.camera(8.5, 46.6, 150_000, 60));
    await expect.poll(async () => (await clip()).mode).toBe("auto");
  });
}

// A translation as large as a position (seen in an Urban3DOL layer: the
// absolute coordinates in both origin and translation fields) is flagged,
// and still placed as the layer says.
test("absolute translation flagged", async ({ page }) => {
  await page.goto("/?view=global&data=w0&terrain=0&buildings=0");
  const result = await page.evaluate(async () => {
    // The dev server's module, imported in the page (not resolved by tsc).
    const module = "/src/arcgis/objectLayer.ts";
    const { buildWrapper } = await import(/* @vite-ignore */ module);
    const roles = { originX: "OX", originY: "OY", originZ: "OZ", translationX: "TX", translationY: "TY",
                    translationZ: "TZ", globalID: "GlobalID" };
    const feature = (oid: number, gid: string, t: number[]) => ({ attributes: {
      OBJECTID: oid, GlobalID: gid, OX: 148902.1, OY: 6568810.9, OZ: 29.95, TX: t[0], TY: t[1], TZ: t[2] } });
    const w = buildWrapper({
      url: "test", format: "3D_glb",
      layer: { spatialReference: { wkid: 3011 }, infoFor3D: { transformFieldRoles: roles } },
      features: [feature(1, "a", [148926.3, 28.7, -6568783.6]), feature(2, "b", [1.5, 0, -2])],
      assetMaps: ["a", "b"].map((g) => ({ parentGlobalId: g, assetType: "3D_glb", assetHash: g,
                                          assetURL: `https://example.invalid/${g}`, assetName: "m.glb" })),
    }, "WKT");
    return { suspicious: w.suspicious, translate: /xformOp:translate = \(148926\.3, 28\.7, -6568783\.6\)/.test(w.usda) };
  });
  expect(Object.keys(result.suspicious)).toEqual(["/ObjectLayer/Feature_1"]);
  expect(result.suspicious["/ObjectLayer/Feature_1"]).toContain("probably absolute coordinates");
  expect(result.translate).toBe(true);
});

// A projected layer's models are in grid directions and lengths: with the
// engine's projection factors the wrapper turns each feature by the meridian
// convergence (crs:orientation heading) and scales its Model by 1 / scale,
// outermost so the translation (grid units too) is scaled as well.
test("grid correction for projected layers", async ({ page }) => {
  await page.goto("/?view=global&data=w0&terrain=0&buildings=0");
  const usda: string = await page.evaluate(async () => {
    const module = "/src/arcgis/objectLayer.ts";
    const { buildWrapper } = await import(/* @vite-ignore */ module);
    const roles = { originX: "OX", originY: "OY", originZ: "OZ", globalID: "GlobalID" };
    const w = buildWrapper({
      url: "test", format: "3D_glb",
      layer: { spatialReference: { wkid: 25832 }, infoFor3D: { transformFieldRoles: roles } },
      features: [{ attributes: { OBJECTID: 1, GlobalID: "a", OX: 298549.5, OY: 6709780.3, OZ: 0 } },
                 { attributes: { OBJECTID: 2, GlobalID: "b", OX: 298600.0, OY: 6709800.0, OZ: 0 } }],
      assetMaps: ["a", "b"].map((g) => ({ parentGlobalId: g, assetType: "3D_glb", assetHash: g,
                                          assetURL: `https://example.invalid/${g}`, assetName: "m.glb" })),
    }, "WKT", [{ meridianConvergence: -3.25, meridionalScale: 1.0001, parallelScale: 1.0001 }, null]);
    return w.usda;
  });
  const [f1, f2] = usda.split('def Xform "Feature_2"');
  expect(f1).toContain("double3 crs:orientation = (-3.25, 0.0, 0.0)");
  expect(f1).toMatch(/double3 xformOp:scale:grid = \(0\.9999\d+, 1\.0, 0\.9999\d+\)/);
  expect(f1).toContain('xformOpOrder = ["xformOp:scale:grid", "xformOp:translate", "xformOp:orient", "xformOp:scale"]');
  expect(f2).not.toContain("crs:orientation");
  expect(f2).not.toContain("scale:grid");
});

// Projected layer, placed: the bergen-utm mock (UTM 32N, 3.19 degrees
// between grid and true north) in the local view, whose render space is
// Web Mercator, against the footprint ArcGIS's grid rule gives (reference.py
// arcgis-mock, pyproj): bounding boxes within 1 cm. Without the grid
// correction the box is metres off.
test("projected layer matches ArcGIS's footprint", async ({ page }) => {
  await page.goto("/?view=local&data=live&terrain=0&buildings=0&layer=/usd/fixtures/arcgis/bergen-utm");
  await page.waitForFunction(() => (globalThis as any).__app?.ready, null, { timeout: 90_000 });
  await expect(page.locator("#log")).toContainText("projected layer: models turned from grid to true north by -3.1905°");
  const expected: Record<string, number[][]> =
    await (await page.request.get("/usd/fixtures/arcgis/bergen-utm/footprint.json")).json();
  const bounds: Record<string, number[][]> = await page.evaluate(() => (globalThis as any).__app.placedBounds());
  const box = Object.entries(bounds).filter(([mesh]) => mesh.includes("/Feature_1/")).map(([, b]) => b);
  expect(box.length).toBeGreaterThan(0);
  const lo = [0, 1].map((k) => Math.min(...box.map((b) => b[0][k])));
  const hi = [0, 1].map((k) => Math.max(...box.map((b) => b[1][k])));
  // Web Mercator units to metres at the layer's latitude.
  const m = Math.cos(2 * Math.atan(Math.exp(expected["1"][0][1] / 6378137)) - Math.PI / 2);
  for (const k of [0, 1]) {
    expect(Math.abs(lo[k] - expected["1"][0][k]) * m).toBeLessThan(0.01);
    expect(Math.abs(hi[k] - expected["1"][1][k]) * m).toBeLessThan(0.01);
  }
});
