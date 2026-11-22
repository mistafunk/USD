// Live data (I1): the OpenUSD module in a worker composes the test scene and
// places it in the view's output CRS through usdImaging and the geospatial
// placement scene index; the observer's anchors and meshes are drawn like
// recorded data (placeScene). Output CRS: EPSG:4979 for a global view (D13),
// the view's spatial reference for a local one.
import type SceneView from "@arcgis/core/views/SceneView.js";
import {
  buildWrapper, crsDefinition, fetchObjectLayer, resolveObjectLayerUrl,
} from "./arcgis/objectLayer";
import { formatBytes, LoadStatus } from "./loadStatus";
import { placeScene, toLonLatH, type RecordedAnchor } from "./recorded";
import type { Scene } from "./scene";
import { USD_BASE, UsdClient } from "./usd/client";
import type { CrsDescription, ModuleInfo, SceneState, StageTestData } from "./usd/types";

export interface LiveOptions {
  /** Measurement mode (T5): exact per-vertex placement instead of matrices. */
  exact: boolean;
  /** An ArcGIS 3D object layer to load through a generated wrapper layer
   *  instead of the test scene: an item id (on `portal`), a feature service
   *  or layer URL, or a mock directory (resolveObjectLayerUrl). */
  layer?: string;
  /** Portal for item ids (default DEFAULT_PORTAL). */
  portal?: string;
  /** Progress and log for the panel. */
  status?: LoadStatus;
  /** Display only (D25): draw anchors lowered by the EGM96 geoid height, to
   *  meet terrain with orthometric heights (World Elevation). */
  geoid?: boolean;
}

/** Portal for item ids and Find: the Esri R&D Center Zurich organization
 *  (public items of other ArcGIS Online organizations resolve there too). */
export const DEFAULT_PORTAL = "https://zurich.maps.arcgis.com";

/** The geoid the display correction uses: World Elevation's heights are
 *  orthometric without a named geoid; EGM96 is global and served (D25). */
export const DISPLAY_GEOID = "EPSG:5773";

/** What the live module returned, for numeric checks (T9). */
export interface LiveData {
  info: ModuleInfo;
  output: { name: string; crs: string; wkt: string; frame: "enu" | "crs" };
  time: number | null;
  exact: boolean;
  anchors: SceneState["anchors"];
  meshes: { path: string; anchor: string | null; status: string; matrix: number[];
            linearizationError: number | null; triangles: number }[];
  /** CRS diagnostics (D27): the output CRS and each anchor's source CRS. */
  crs: { output: CrsDescription; anchors: Record<string, CrsDescription> };
  /** The stage's test cases (empty for object layers). */
  testCases: StageTestData["testCases"];
  timings: Record<string, number>;
  /** Grids fetched for this placement (D23). */
  grids: string[];
  /** Display correction (D25): geoid heights N per anchor path, metres. */
  geoid?: { crs: string; operation: string; heights: Record<string, number> };
  /** 3D object layers: the CRS used, the generated wrapper and skipped features. */
  objectLayer?: { crs: string; wrapper: string; skipped: string[] };
}

interface SceneIndex {
  stage: string;
  files: string[];
}

export async function loadLiveScene(
  view: SceneView, options: LiveOptions,
): Promise<{ scene: Scene; live: LiveData }> {
  const timings: Record<string, number> = {};
  const timed = async <T>(label: string, fn: () => Promise<T>): Promise<T> => {
    const t0 = performance.now();
    try {
      return await fn();
    } finally {
      timings[label] = Math.round(performance.now() - t0);
    }
  };

  const status = options.status ?? new LoadStatus();
  const log = (text: string) => status.add("info", text);
  const usd = new UsdClient();
  usd.onProgress = (p) => status.bytes(p.label, p.loaded, p.total);
  usd.onLog = (level, text) => status.add(level, text);
  status.step("starting the OpenUSD module");
  const info = await timed("init", () => usd.init());
  log(`module: engines ${info.engines.join(", ")}, ${info.threads} work threads`);
  let objectLayer: LiveData["objectLayer"];
  let fallbackTarget: [number, number, number] | undefined;
  if (options.layer) {
    // A 3D object layer: models fetched as they are, placed by a wrapper.
    status.step("reading the 3D object layer");
    const url = await resolveObjectLayerUrl(options.layer, options.portal ?? DEFAULT_PORTAL, log);
    const data = await timed("layer", () => fetchObjectLayer(url, log));
    const { definition, promote3D } = crsDefinition(data.layer.spatialReference);
    const crs = await usd.crsWkt(definition, promote3D);
    if (!crs.wkt) throw new Error(`CRS ${definition}: ${crs.error}`);
    log(`CRS ${definition}${promote3D ? " promoted to 3D (z taken as ellipsoidal)" : ""}`);
    const wrapper = buildWrapper(data, crs.wkt);
    wrapper.skipped.forEach((s) => status.add("warn", s));
    if (!wrapper.models.length) throw new Error("no feature has a glb model");
    status.step(`downloading ${wrapper.models.length} model(s)`);
    wrapper.models.forEach((m) => log(`model ${m.path} from ${m.url}`));
    await timed("models", () => usd.fetchFiles(
      wrapper.models.map((m) => ({ url: m.url, path: `/arcgis/${m.path}` }))));
    const d = status.downloads();
    log(`downloaded ${formatBytes(d.loaded)}`);
    await usd.writeText("/arcgis/layer.usda", wrapper.usda);
    status.step("opening the wrapper layer (reads the glb models)");
    await timed("load", () => usd.open("/arcgis/layer.usda"));
    objectLayer = { crs: definition + (promote3D ? " (3D)" : ""), wrapper: wrapper.usda,
                    skipped: wrapper.skipped };
    const first = data.features[0]?.attributes;
    const roles = data.layer.infoFor3D.transformFieldRoles;
    if (first && (data.layer.spatialReference.latestWkid ?? data.layer.spatialReference.wkid) === 4326) {
      fallbackTarget = [Number(first[roles.originX]), Number(first[roles.originY]),
                        Number(first[roles.originZ])];
    }
  } else {
    status.step("loading the test scene");
    const base = `${USD_BASE}/fixtures/scene`;
    const res = await fetch(`${base}/index.json`);
    if (!res.ok) throw new Error(`${res.url}: ${res.status} (run: geo.py build wasm32)`);
    const index = (await res.json()) as SceneIndex;
    await timed("load", () => usd.load(
      index.files.map((f) => ({ url: `${base}/${f}`, path: `/scene/${f}` })),
      `/scene/${index.stage}`));
  }

  const test: StageTestData = await usd.testData();
  if (test.time !== null) await usd.setTime(test.time);

  const sr = view.spatialReference;
  const global = view.viewingMode === "global";
  const crs = global ? "EPSG:4979" : `EPSG:${sr.latestWkid ?? sr.wkid}`;
  const outputName = global ? "global" : `local-${sr.latestWkid ?? sr.wkid}`;
  status.step(`placing in ${crs}`);
  await timed("setOutput", () => usd.setOutput(crs, true, options.exact));
  let state = await timed("sync", () => usd.state());
  // Grids the placement reports missing (D23): fetch those the server has,
  // then place again with them.
  const missing = state.anchors.flatMap((a) => a.missingGrids ?? []);
  if (missing.length) status.step(`fetching grids: ${[...new Set(missing)].join(", ")}`);
  const grids = missing.length ? await timed("grids", () => usd.ensureGrids(missing)) : [];
  if (missing.length) log(grids.length ? `fetched ${grids.join(", ")}` : "none of them is served");
  if (grids.length) {
    status.step(`placing again with ${grids.length} grid(s)`);
    await usd.setOutput(crs, true, options.exact);
    state = await timed("sync with grids", () => usd.state());
  }
  // Per anchor: result and meshes, so a model that did not load shows up.
  for (const a of state.anchors) {
    const meshes = state.meshes.filter((m) => m.anchor === a.path);
    const triangles = meshes.reduce((n, m) => n + m.indices.length / 3, 0);
    const line = `${a.path}: ${a.status}, ${meshes.length} mesh(es), ${triangles} triangles; ` +
      (a.status === "ok"
        ? `origin ${a.origin.map((v) => v.toFixed(a.origin[0] > 360 ? 3 : 8)).join(", ")}; ${a.operation}`
        : a.reason);
    status.add(a.status !== "ok" ? "error" : meshes.length ? "info" : "warn",
               meshes.length || a.status !== "ok" ? line : line + " (no geometry: see USD messages)");
  }

  // Display correction (D25): geoid heights at the placed anchors.
  let geoid: LiveData["geoid"];
  let displayOffsets: Map<string, number> | undefined;
  const placed = state.anchors.filter((a) => a.status === "ok");
  if (options.geoid && placed.length) {
    status.step(`geoid heights (${DISPLAY_GEOID}) for the display`);
    const points = placed.map((a) => toLonLatH(state.frame, a.origin).slice(0, 2) as [number, number]);
    const r = await timed("geoid", () => usd.geoidHeights(points, DISPLAY_GEOID));
    if (r.heights.length) {
      displayOffsets = new Map(placed.map((a, i) => [a.path, r.heights[i]]));
      geoid = { crs: DISPLAY_GEOID, operation: r.operation,
                heights: Object.fromEntries(displayOffsets) };
      placed.forEach((a, i) => status.add("info",
        `${a.path}: drawn ${r.heights[i].toFixed(2)} m lower (geoid height, ${r.operation})`));
    } else {
      status.add("warn", `no geoid correction: ${r.error}` +
                 (r.missingGrids.length ? ` (grids not served: ${r.missingGrids.join(", ")})` : ""));
    }
  }

  // Exact mode scores control points at their exact output positions.
  let exactPoints: Map<string, number[]> | undefined;
  if (options.exact) {
    exactPoints = new Map();
    const ok = new Set(state.anchors.filter((a) => a.status === "ok").map((a) => a.path));
    for (const anchor of ok) {
      const cps = test.controlPoints.filter((cp) => cp.anchor === anchor && cp.local);
      if (!cps.length) continue;
      const r = await usd.resolve(anchor, crs, cps.map((cp) => cp.local as [number, number, number]));
      r.outputPoints?.forEach((p, i) => exactPoints!.set(cps[i].path, p));
    }
  }
  // CRS diagnostics (D27), before the worker ends.
  const crsInfo = {
    output: await usd.describeCrs(state.outputWkt || crs),
    anchors: await usd.anchorCrs(state.anchors.map((a) => a.path)),
  };
  usd.terminate();

  const engine = state.anchors.find((a) => a.engine)?.engine ?? info.engine;
  const anchors: RecordedAnchor[] = state.anchors.map((a) => ({
    ...a,
    reason: a.reason || undefined,
  }));
  const scene = placeScene(view, {
    name: `live ${outputName} (${engine} in the browser${options.exact ? ", exact points" : ""})`,
    output: { name: outputName, crs, frame: state.frame },
    engine: `${engine} (browser)`,
    anchors,
    meshes: state.meshes.map((m) => ({ ...m, positions: m.points })),
    controlPoints: test.controlPoints,
    testCases: test.testCases,
    exactPoints,
    fallbackTarget,
    live: true,
    displayOffsets,
  });
  return {
    scene,
    live: {
      info,
      output: { name: outputName, crs, wkt: state.outputWkt, frame: state.frame },
      time: test.time,
      exact: options.exact,
      anchors: state.anchors,
      meshes: state.meshes.map((m) => ({
        path: m.path, anchor: m.anchor, status: m.status, matrix: m.matrix,
        linearizationError: m.linearizationError, triangles: m.indices.length / 3,
      })),
      crs: crsInfo,
      testCases: test.testCases,
      timings,
      objectLayer,
      grids,
      geoid,
    },
  };
}
