// Live data (I1): the OpenUSD module in a worker composes the test scene and
// places it in the view's output CRS through usdImaging and the geospatial
// placement scene index; the observer's anchors and meshes are drawn like
// recorded data (placeScene). Output CRS: EPSG:4979 for a global view (D13),
// the view's spatial reference for a local one.
import type SceneView from "@arcgis/core/views/SceneView.js";
import { buildWrapper, crsDefinition, fetchObjectLayer } from "./arcgis/objectLayer";
import { placeScene, type RecordedAnchor } from "./recorded";
import type { Scene } from "./scene";
import { USD_BASE, UsdClient } from "./usd/client";
import type { ModuleInfo, SceneState, StageTestData } from "./usd/types";

export interface LiveOptions {
  /** Measurement mode (T5): exact per-vertex placement instead of matrices. */
  exact: boolean;
  /** An ArcGIS 3D object layer (.../FeatureServer/<id>, or a mock directory)
   *  to load through a generated wrapper layer instead of the test scene. */
  layer?: string;
}

/** What the live module returned, for numeric checks (T9). */
export interface LiveData {
  info: ModuleInfo;
  output: { name: string; crs: string; wkt: string; frame: "enu" | "crs" };
  time: number | null;
  exact: boolean;
  anchors: SceneState["anchors"];
  meshes: { path: string; status: string; matrix: number[];
            linearizationError: number | null }[];
  timings: Record<string, number>;
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

  const usd = new UsdClient();
  const info = await timed("init", () => usd.init());
  let objectLayer: LiveData["objectLayer"];
  let fallbackTarget: [number, number, number] | undefined;
  if (options.layer) {
    // A 3D object layer: models fetched as they are, placed by a wrapper.
    const data = await timed("layer", () => fetchObjectLayer(options.layer!));
    const { definition, promote3D } = crsDefinition(data.layer.spatialReference);
    const crs = await usd.crsWkt(definition, promote3D);
    if (!crs.wkt) throw new Error(`CRS ${definition}: ${crs.error}`);
    const wrapper = buildWrapper(data, crs.wkt);
    await timed("models", () => usd.fetchFiles(
      wrapper.models.map((m) => ({ url: m.url, path: `/arcgis/${m.path}` }))));
    await usd.writeText("/arcgis/layer.usda", wrapper.usda);
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
  await timed("setOutput", () => usd.setOutput(crs, true, options.exact));
  const state = await timed("sync", () => usd.state());

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
        path: m.path, status: m.status, matrix: m.matrix,
        linearizationError: m.linearizationError,
      })),
      timings,
      objectLayer,
    },
  };
}
