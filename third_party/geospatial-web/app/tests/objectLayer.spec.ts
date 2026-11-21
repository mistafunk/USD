// ArcGIS 3D object layers through a wrapper layer (D24): the app reads a
// layer (mocks in testdata/arcgis, the REST API's shape; reference.py
// arcgis-mock), generates a wrapper with the geospatial CRS schemas and loads
// the glb models through the Adobe glTF plugin, all in the browser module.
//   zurich-ellipsoidal  WGS 84 without vertical CRS: placed at the origins
//   zurich-egm96        WGS 84 + EGM96 heights: unsupported until the EGM96
//                       grid is available (D23)
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

async function open(page: Page, view: string, layer: string): Promise<{ app: App; live: LiveData }> {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(`/?view=${view}&data=live&layer=/usd/fixtures/arcgis/${layer}`);
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
  // EGM96 heights need the geoid grid, which the browser does not have yet.
  expect(live.objectLayer!.crs).toBe("EPSG:4326+5773");
  for (const a of live.anchors) {
    expect(a.status).toBe("unsupported");
    expect(a.reason).toMatch(/egm96/i);
  }
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
