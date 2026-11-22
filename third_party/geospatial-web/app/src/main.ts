// Geospatial web app: a RenderNode draws placed meshes in a global or local
// SceneView on a cross-origin isolated page.
//
//   ?view=global|local   viewing mode (default global)
//   &data=live|recorded|w0  the live OpenUSD module (I1, default), recorded
//                        data (W1; &src=recorded-cpp for the C++ dump) or
//                        the W0 hard-coded meshes
//   &exact=1             live only: exact per-vertex placement (measurement
//                        mode, T5) instead of matrices
//   &naive=1             W0 only: bake absolute render coordinates into
//                        float32 vertices (shows the jitter we avoid)
//   &terrain=1|0         Esri World Elevation as ground (default: on for
//                        &layer=, off for the test scene, which is flat)
//   &geoid=1|0           live only, display only (D25): lower drawn anchors by
//                        the EGM96 geoid height to meet the terrain, whose
//                        orthometric heights the view draws as ellipsoidal
//                        (default: as terrain)
//   &buildings=1|0       Open 3D Buildings scene layer as visual reference
//                        (default: on for &layer=; the panel toggles it live)

import esriConfig from "@arcgis/core/config.js";
import Basemap from "@arcgis/core/Basemap.js";
import Camera from "@arcgis/core/Camera.js";
import Ground from "@arcgis/core/Ground.js";
import Map from "@arcgis/core/Map.js";
import ElevationLayer from "@arcgis/core/layers/ElevationLayer.js";
import SceneLayer from "@arcgis/core/layers/SceneLayer.js";
import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import TileLayer from "@arcgis/core/layers/TileLayer.js";
import SceneView from "@arcgis/core/views/SceneView.js";
import { watch } from "@arcgis/core/core/reactiveUtils.js";
import type { HarnessReport } from "./harness";
import { transformPoint } from "./mat4";
import { MeshPass, type PassStats, type ScreenError } from "./meshPass";
import { findObjectLayers } from "./arcgis/discover";
import { DEFAULT_PORTAL, loadLiveScene, type LiveData } from "./live";
import { formatBytes, LoadStatus } from "./loadStatus";
import { loadRecordedScene } from "./recorded";
import type { CameraPreset, Scene } from "./scene";
import { w0Scene } from "./w0Scene";

// Self-hosted SDK assets (see vite.config.ts); the CDN worker is blocked by COEP.
esriConfig.assetsPath = __ARCGIS_ASSETS__;

const params = new URLSearchParams(location.search);
const viewingMode = params.get("view") === "local" ? "local" : "global";
// Content: the live OpenUSD module (default, D21), recorded data, or W0's
// hard-coded meshes.
const data = (["recorded", "w0"] as const).find((d) => d === params.get("data")) ?? "live";
// Measurement mode (T5): exact per-vertex placement instead of matrices.
const exact = params.get("exact") === "1";
// Live only: an ArcGIS 3D object layer instead of the test scene: a
// .../FeatureServer[/<id>] URL (query string ignored), an item page URL
// (.../home/item.html?id=...), an item id (on &portal=, default
// zurich.maps.arcgis.com) or a mock directory such as
// /usd/fixtures/arcgis/zurich-egm96.
const objectLayer = params.get("layer") ?? undefined;
const portal = params.get("portal") ?? undefined;
const loadStatus = new LoadStatus();
// Directory of recorded data under public/: the offline reference's export
// (recorded) or the C++ observer's (recorded-cpp, geo.py record).
const recordedDir = /^[a-z0-9-]+$/.test(params.get("src") ?? "")
  ? params.get("src")!
  : "recorded";
const naive = data === "w0" && params.get("naive") === "1";
const M_PER_DEG_LAT = 111320;

/** Test and debugging hooks, read by the Playwright specs. */
interface AppState {
  ready: boolean;
  updating: boolean;
  viewingMode: string;
  data: string;
  scene: string;
  crossOriginIsolated: boolean;
  sharedArrayBuffer: boolean;
  /** WebGL renderer string, e.g. a GPU or SwiftShader (software). */
  renderer: string;
  stats: PassStats | null;
  anchors: Scene["anchors"];
  errors: string[];
  goto?: (preset: string) => Promise<unknown>;
  /** Render-space position (Float64) of a mesh-local point. */
  renderPoint?: (mesh: string, p: number[]) => number[] | null;
  /** T6: screen error of a mesh point, float32 pipeline vs Float64. */
  screenError?: (mesh: string, p: number[]) => ScreenError | null;
  /** W2 harness: control point scoring for the current view. */
  score?: () => HarnessReport | null;
  report: HarnessReport | null;
  /** Live data: what the module returned (T9 checks). */
  live: LiveData | null;
}

const state: AppState = {
  ready: false,
  updating: true,
  viewingMode,
  data,
  scene: "",
  crossOriginIsolated: globalThis.crossOriginIsolated === true,
  sharedArrayBuffer: typeof SharedArrayBuffer !== "undefined",
  renderer: webglRenderer(),
  stats: null,
  anchors: [],
  errors: [],
  report: null,
  live: null,
};
(globalThis as unknown as { __app: AppState }).__app = state;
addEventListener("error", (e) => state.errors.push(String(e.message)));
addEventListener("unhandledrejection", (e) => state.errors.push(String(e.reason)));

function webglRenderer(): string {
  const gl = document.createElement("canvas").getContext("webgl2");
  if (!gl) return "no WebGL2";
  const info = gl.getExtension("WEBGL_debug_renderer_info");
  const name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
  gl.getExtension("WEBGL_lose_context")?.loseContext();
  return name;
}

// Keyless Esri imagery tiles (Web Mercator tiling, so it works in both modes).
const basemap = new Basemap({
  baseLayers: [new TileLayer({
    url: "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer",
  })],
});

// Terrain: Esri's World Elevation (keyless). Its heights are taken as is by
// the SceneView (no geoid applied), the test scene stays on flat ground.
const terrainParam = new URLSearchParams(location.search).get("terrain");
const terrain = terrainParam ? terrainParam === "1"
  : new URLSearchParams(location.search).has("layer");
const ground = new Ground({
  layers: terrain ? [new ElevationLayer({
    url: "https://elevation3d.arcgis.com/arcgis/rest/services/WorldElevation3D/Terrain3D/ImageServer",
  })] : [],
  // Let the camera go down to street level (the default stay-above
  // constraint keeps it several metres up).
  navigationConstraint: { type: "none" },
});

const geoidParam = new URLSearchParams(location.search).get("geoid");
const geoid = geoidParam ? geoidParam === "1" : terrain;

// Visual reference: Esri's Open 3D Buildings (ArcGIS Online item).
const OPEN_3D_BUILDINGS = "c444b24b184c4523a5dc96248bfea4e1";
const buildingsParam = new URLSearchParams(location.search).get("buildings");
const buildings = buildingsParam ? buildingsParam === "1"
  : new URLSearchParams(location.search).has("layer");

const view = new SceneView({
  container: "viewDiv",
  map: new Map({ basemap, ground }),
  viewingMode,
  spatialReference: viewingMode === "local" ? SpatialReference.WebMercator : undefined,
});

let scene: Scene | null = null;

function cameraFor(s: Scene, p: CameraPreset): Camera {
  const [lon, lat, h] = s.target;
  const mPerDegLon = M_PER_DEG_LAT * Math.cos((lat * Math.PI) / 180);
  return new Camera({
    position: {
      longitude: lon + p.east / mPerDegLon,
      latitude: lat + p.north / M_PER_DEG_LAT,
      z: h + p.up,
      spatialReference: SpatialReference.WGS84,
    },
    heading: p.heading,
    tilt: p.tilt,
  });
}

/**
 * Moves the camera. The SDK picks near/far clip distances from the scene it
 * knows about, which excludes RenderNode geometry, so close to our meshes it
 * clips them; close-up presets use a fixed 10 cm near plane.
 */
function goto(name: string, animate: boolean): Promise<unknown> {
  if (!scene) return Promise.resolve();
  const preset = scene.presets[name];
  if (!preset) return Promise.reject(new Error("unknown preset " + name));
  const clip = view.constraints.clipDistance;
  if (preset.near) {
    clip.near = 0.1;
    clip.far = 50_000;
  } else {
    clip.mode = "auto";
  }
  return view.goTo(cameraFor(scene, preset), { animate });
}

watch(() => view.updating, (updating) => { state.updating = updating; },
      { initial: true });

const pass = new MeshPass(view);
state.stats = pass.stats;

view.when(async () => {
  if (data === "live") {
    const loaded = await loadLiveScene(view, { exact, layer: objectLayer, portal,
                                               status: loadStatus, geoid });
    scene = loaded.scene;
    state.live = loaded.live;
  } else {
    scene = data === "w0" ? w0Scene(view, naive) : await loadRecordedScene(view, recordedDir);
  }
  state.scene = scene.name;
  state.anchors = scene.anchors;
  pass.setMeshes(scene.meshes);
  buildPresetButtons(scene);
  await goto("overview", false);
  state.goto = (preset) => goto(preset, false);
  state.renderPoint = (mesh, p) => {
    const m = scene?.renderMatrices.get(mesh);
    return m ? transformPoint(m, p) : null;
  };
  state.screenError = (mesh, p) => pass.screenError(mesh, p);
  state.score = () => {
    const r = scene?.score?.() ?? null;
    if (r) r.renderer = state.renderer;
    return (state.report = r);
  };
  state.ready = true;
  loadStatus.finish(`ready: ${scene.meshes.length} mesh(es) drawn`);
}).catch((err: unknown) => {
  state.errors.push("scene: " + String(err));
  loadStatus.fail(err);
});

// --- panel ---------------------------------------------------------------

function setLinks() {
  document.querySelectorAll<HTMLAnchorElement>("a[data-view], a[data-data]").forEach((a) => {
    const p = new URLSearchParams(location.search);
    if (a.dataset.view) p.set("view", a.dataset.view);
    if (a.dataset.data) p.set("data", a.dataset.data);
    a.href = "?" + p.toString();
    a.classList.toggle("active", a.dataset.view === viewingMode || a.dataset.data === data);
  });
}
setLinks();

const exactBox = document.getElementById("exact") as HTMLInputElement;
exactBox.checked = exact;
exactBox.disabled = data !== "live";
exactBox.addEventListener("change", () => {
  const p = new URLSearchParams(location.search);
  if (exactBox.checked) p.set("exact", "1"); else p.delete("exact");
  location.search = p.toString();
});

const terrainBox = document.getElementById("terrain") as HTMLInputElement;
terrainBox.checked = terrain;
terrainBox.addEventListener("change", () => {
  const p = new URLSearchParams(location.search);
  p.set("terrain", terrainBox.checked ? "1" : "0");
  location.search = p.toString();
});

// Open 3D Buildings: added on first use, then only shown or hidden (no
// reload; the URL keeps the choice). A layer that cannot be shown (not
// shared, or a global layer in the local Web Mercator view) is a warning in
// the load log, not an app error.
let buildingsLayer: SceneLayer | null = null;
function showBuildings(on: boolean) {
  if (on && !buildingsLayer) {
    const layer = new SceneLayer({ portalItem: { id: OPEN_3D_BUILDINGS } });
    buildingsLayer = layer;
    view.map!.add(layer);
    view.whenLayerView(layer)
      .then(() => loadStatus.add("info", `buildings: "${layer.title}" shown`))
      .catch((e: unknown) => loadStatus.add("warn",
        `buildings not shown: ${String((e as Error)?.message ?? e)}`));
  }
  if (buildingsLayer) buildingsLayer.visible = on;
}
const geoidBox = document.getElementById("geoid") as HTMLInputElement;
geoidBox.checked = geoid;
geoidBox.disabled = data !== "live";
geoidBox.addEventListener("change", () => {
  const p = new URLSearchParams(location.search);
  p.set("geoid", geoidBox.checked ? "1" : "0");
  location.search = p.toString();
});

const buildingsBox = document.getElementById("buildings") as HTMLInputElement;
buildingsBox.checked = buildings;
showBuildings(buildings);
buildingsBox.addEventListener("change", () => {
  showBuildings(buildingsBox.checked);
  const p = new URLSearchParams(location.search);
  p.set("buildings", buildingsBox.checked ? "1" : "0");
  history.replaceState(null, "", "?" + p.toString());
  setLinks();
});

const naiveBox = document.getElementById("naive") as HTMLInputElement;
naiveBox.checked = naive;
naiveBox.disabled = data !== "w0";
naiveBox.addEventListener("change", () => {
  const p = new URLSearchParams(location.search);
  if (naiveBox.checked) p.set("naive", "1"); else p.delete("naive");
  location.search = p.toString();
});

function buildPresetButtons(s: Scene) {
  const row = document.getElementById("presets")!;
  row.replaceChildren(...Object.entries(s.presets).map(([name, p]) => {
    const b = document.createElement("button");
    b.textContent = p.label;
    b.addEventListener("click", () => goto(name, true));
    return b;
  }));
}

// Layer combobox: loads a 3D object layer live (sets the URL's layer); the
// presets are suggestions, any URL, item id or item page can be typed.
const LAYER_PRESETS: [string, string][] = [
  ["https://services2.arcgis.com/cFEFS0EWrhfDeVw9/arcgis/rest/services/Zurich_Office_4th_Floor/FeatureServer/0",
   "Zurich Office 4th Floor (EGM96 heights, about 180 MiB of models)"],
  ["/usd/fixtures/arcgis/zurich-egm96", "mock: Zurich boxes, EGM96 heights"],
  ["/usd/fixtures/arcgis/zurich-ellipsoidal", "mock: Zurich boxes, no vertical CRS"],
  ["/usd/fixtures/arcgis/transform", "mock: feature transform"],
];
const itemForm = document.getElementById("itemForm") as HTMLFormElement;
const itemInput = document.getElementById("item") as HTMLInputElement;
const presetList = document.getElementById("itemPresets")!;
function setPresets(presets: [string, string][]) {
  presetList.replaceChildren(...presets.map(([value, label]) => {
    const o = document.createElement("option");
    o.value = value;
    o.label = label;
    return o;
  }));
}
// Find results are kept in this browser across page loads (loading a layer
// reloads the page); without storage only the built-in presets remain.
const FOUND_KEY = "geospatial-web.foundLayers";
function storedFound(): [string, string][] {
  try {
    const v = JSON.parse(localStorage.getItem(FOUND_KEY) ?? "[]");
    return Array.isArray(v) ? v.filter((e) => Array.isArray(e) && e.length === 2) : [];
  } catch {
    return [];
  }
}
function withBuiltIn(found: [string, string][]): [string, string][] {
  return [...found, ...LAYER_PRESETS.filter(([v]) => !found.some(([f]) => f === v))];
}
setPresets(withBuiltIn(storedFound()));

// Find: the organization's public 3D object layers with models, as presets.
// Organization: a portal address typed in the field, else &portal=, else
// DEFAULT_PORTAL (the Zurich org).
const findButton = document.getElementById("find") as HTMLButtonElement;
findButton.addEventListener("click", async () => {
  const typed = /^https?:\/\/[^/?#]+(\/portal)?\/?$/.test(itemInput.value.trim())
    ? itemInput.value.trim() : "";
  const org = typed || portal || DEFAULT_PORTAL;
  loadbar.style.display = "";
  logBox.open = true;
  findButton.disabled = true;
  loadStatus.add("info", `finding 3D object layers on ${org}`);
  try {
    const found = await findObjectLayers(org, (t) => loadStatus.add("info", t),
      (done, total) => { findButton.textContent = `Find ${done}/${total}`; });
    const entries = found.map((f): [string, string] =>
      [f.url, `${f.title} (${f.features} features, ${f.crs})`]);
    setPresets(withBuiltIn(entries));
    try {
      localStorage.setItem(FOUND_KEY, JSON.stringify(entries));
    } catch {
      // Not kept across page loads, still listed now.
    }
    loadStatus.add("info", `found ${found.length} loadable layers on ${org}; pick one in the field`);
    if (typed) itemInput.value = "";
    itemInput.focus();
  } catch (e) {
    loadStatus.add("error", `find on ${org}: ${String((e as Error)?.message ?? e)}`);
  } finally {
    findButton.disabled = false;
    findButton.textContent = "Find";
  }
});
itemInput.value = objectLayer ?? "";
itemForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const p = new URLSearchParams(location.search);
  p.set("data", "live");
  if (itemInput.value.trim()) p.set("layer", itemInput.value.trim()); else p.delete("layer");
  location.search = p.toString();
});

// Load status: phase, download progress, log.
const loadbar = document.getElementById("loadbar")!;
const phaseEl = document.getElementById("phase")!;
const progressEl = document.getElementById("progress") as HTMLProgressElement;
const logEl = document.getElementById("log")!;
const logBox = document.getElementById("logbox") as HTMLDetailsElement;
loadbar.style.display = data === "live" ? "" : "none";
let renderQueued = false;
loadStatus.onChange(() => {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => {
    renderQueued = false;
    const d = loadStatus.downloads();
    const downloading = d.active > 0 && !loadStatus.done;
    phaseEl.textContent = loadStatus.phase + (downloading
      ? ` - ${formatBytes(d.loaded)}${d.total ? ` of ${formatBytes(d.total)}` : ""}` : "");
    phaseEl.className = loadStatus.failed ? "failed" : loadStatus.done ? "done" : "";
    loadbar.classList.toggle("done", loadStatus.done);
    if (downloading && d.total) progressEl.value = d.loaded / d.total;
    else progressEl.removeAttribute("value");  // indeterminate
    if (loadStatus.failed) logBox.open = true;
    logEl.replaceChildren(...loadStatus.log.map((e) => {
      const line = document.createElement("div");
      line.className = e.level;
      line.textContent = `${e.t.toFixed(1).padStart(6)}s ${e.text}`;
      return line;
    }));
    logEl.scrollTop = logEl.scrollHeight;
  });
});

const scoreButton = document.getElementById("score") as HTMLButtonElement;
scoreButton.addEventListener("click", () => state.score?.());

function reportLines(r: HarnessReport | null): string[] {
  if (!r) return [];
  const s = r.summary;
  return [
    `score (tolerance ${r.tolerance} m): ${s.pass} pass, ${s.fail} fail, ` +
      `${s.unsupported} unsupported, ${s.unexpected} unexpected`,
    ...r.cases.map((c) => `${c.ok ? "  " : "! "}${c.result.padEnd(11)} ${c.anchor}` +
      ` (expected ${c.expect}) ${c.detail}`),
  ];
}

const statusEl = document.getElementById("status")!;
setInterval(() => {
  const cam = view.camera;
  statusEl.textContent = [
    `${viewingMode}: ${state.scene || "loading"}`,
    `crossOriginIsolated ${state.crossOriginIsolated}  SAB ${state.sharedArrayBuffer}`,
    `WebGL ${state.renderer}`,
    `frames ${pass.stats.frames}  meshes ${pass.stats.meshes}  tris ${pass.stats.triangles}`,
    `near ${pass.stats.near.toFixed(2)} m (${view.constraints.clipDistance.mode})` +
      `  camera z ${cam?.position.z?.toFixed(2) ?? "-"} m`,
    ...state.anchors.map((a) => `${a.status.padEnd(11)} ${a.path}\n            ${a.detail}`),
    ...reportLines(state.report),
    state.errors.length ? `errors: ${state.errors.join("; ")}` : "",
  ].filter(Boolean).join("\n");
}, 250);
