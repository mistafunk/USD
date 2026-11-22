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
//   &layer=<url|item>    live only: an ArcGIS 3D object layer instead of the
//                        test scene; the panel loads others in place
//   &terrain=1|0         Esri World Elevation as ground (default: on for
//                        &layer=, off for the test scene, which is flat); it
//                        sets the height model the output CRS follows (D28):
//                        gravity-related (EGM96) with terrain, else ellipsoidal
//   &buildings=1|0       Open 3D Buildings scene layer as visual reference
//                        (default: on for &layer=)
// Layers, terrain and buildings change in place (the URL follows; terrain
// places the content again); view, content, exact and naive reload the page.

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
import { watch, whenOnce } from "@arcgis/core/core/reactiveUtils.js";
import { fromRenderCoordinates } from "@arcgis/core/views/3d/webgl.js";
import { score } from "./checks/harness";
import { transformPoint } from "./mat4";
import { MeshPass, type PassStats, type ScreenError } from "./meshPass";
import { DEFAULT_PORTAL, heightModelOf, loadLiveScene, type HeightModel, type LiveData } from "./live";
import { LoadStatus } from "./loadStatus";
import { loadRecordedScene } from "./checks/recorded";
import type { CameraPreset, HarnessReport, Scene } from "./scene";
import { initAbout } from "./ui/about";
import { FeaturesTab } from "./ui/features";
import { LayersTab, TEST_LAYERS } from "./ui/layers";
import { bindDiagnostics, bindLoadStatus, initPanel, showTab } from "./ui/panel";
import { w0Scene } from "./spike/w0Scene";

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
// Loading another layer replaces it in place (loadLayer), without a reload.
let objectLayer = params.get("layer") ?? undefined;
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
  /** Bounding box of each mesh's placed vertices in render space, [min xyz,
   *  max xyz] (specs: e.g. against a layer's footprints in a local view,
   *  whose render space is its spatial reference). Boxes, not vertices: a
   *  layer can have millions, too many to hand to a test. */
  placedBounds?: () => Record<string, [number[], number[]]>;
  /** T6: screen error of a mesh point, float32 pipeline vs Float64. */
  screenError?: (mesh: string, p: number[]) => ScreenError | null;
  /** W2 harness: control point scoring for the current view. */
  score?: () => HarnessReport | null;
  report: HarnessReport | null;
  /** Live data: what the module returned (T9 checks). */
  live: LiveData | null;
  /** Completed loads in this page (a layer loaded in place adds one). */
  loads: number;
  /** Loads a 3D object layer in place (no page reload). */
  loadLayer?: (url: string) => Promise<void>;
  /** Panel: selects an anchor row and highlights its meshes (null: none). */
  select?: (path: string | null) => void;
  /** Panel: draws only an anchor's meshes (null: all). */
  solo?: (path: string | null) => void;
  /** Panel: mutes or unmutes an anchor (null: unmutes all). */
  mute?: (path: string | null, on?: boolean) => void;
  /** Panel: zooms to an anchor's meshes. */
  zoom?: (path: string) => Promise<unknown>;
  /** The view's clip distances now (specs). */
  clip?: () => { mode: string; near: number; far: number };
  /** Moves the camera (specs: navigating away from the content). */
  camera?: (lon: number, lat: number, z: number, tilt: number) => Promise<unknown>;
  showTab?: (name: string) => void;
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
  loads: 0,
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

/** A 1/0 URL parameter, defaulting to on when a 3D object layer is loaded. */
function layerDefault(param: string, layer: string | undefined): boolean {
  const v = new URLSearchParams(location.search).get(param);
  return v ? v === "1" : !!layer;
}

/** Sets or clears URL parameters without reloading (the page keeps its state). */
function setParams(values: Record<string, string | null>, push = false) {
  const p = new URLSearchParams(location.search);
  for (const [k, v] of Object.entries(values)) {
    if (v === null) p.delete(k); else p.set(k, v);
  }
  if (push) history.pushState(null, "", "?" + p.toString());
  else history.replaceState(null, "", "?" + p.toString());
  setLinks();
}

// Terrain: Esri's World Elevation (keyless), added and removed in place. Its
// heights are taken as is by the SceneView (no geoid applied); the test
// scene stays on flat ground.
const ground = new Ground({
  // Let the camera go down to street level (the default stay-above
  // constraint keeps it several metres up).
  navigationConstraint: { type: "none" },
});
let elevation: ElevationLayer | null = null;
function setTerrain(on: boolean) {
  if (on && !elevation) {
    elevation = new ElevationLayer({
      url: "https://elevation3d.arcgis.com/arcgis/rest/services/WorldElevation3D/Terrain3D/ImageServer",
    });
  }
  if (!elevation) return;
  if (on && !ground.layers.includes(elevation)) ground.layers.add(elevation);
  if (!on) ground.layers.remove(elevation);
}
let terrain = layerDefault("terrain", objectLayer);
setTerrain(terrain);

// Visual reference: Esri's Open 3D Buildings (ArcGIS Online item).
const OPEN_3D_BUILDINGS = "c444b24b184c4523a5dc96248bfea4e1";
const buildings = layerDefault("buildings", objectLayer);

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
 * Clip distances for a camera `distance` metres from the content. The SDK
 * picks near/far from the scene it knows about, which excludes RenderNode
 * geometry, so close to our meshes it clips them: within CLOSE a fixed 10 cm
 * near plane (and 50 km far), else the SDK's automatic distances. Applied
 * before each camera move of the app and again whenever the camera comes to
 * rest, so navigating away (also after switching between global and local,
 * which reloads the page) never keeps the close-up's 50 km far plane.
 */
const CLOSE = 300;
function setClip(distance: number) {
  const clip = view.constraints.clipDistance;
  if (distance < CLOSE) {
    if (clip.mode !== "manual" || clip.near !== 0.1 || clip.far !== 50_000) {
      clip.near = 0.1;
      clip.far = 50_000;
    }
  } else if (clip.mode !== "auto") {
    clip.mode = "auto";
  }
}

/** What the camera last went to (preset target or zoomed meshes): lon, lat, h. */
let focus: number[] | null = null;

/** Metres from the camera to the focus (Infinity without one). */
function cameraDistance(): number {
  const p = view.camera?.position;
  if (!focus || !p || p.longitude == null || p.latitude == null) return Infinity;
  const [lon, lat, h] = focus;
  const east = (p.longitude - lon) * M_PER_DEG_LAT * Math.cos((lat * Math.PI) / 180);
  const north = (p.latitude - lat) * M_PER_DEG_LAT;
  return Math.hypot(east, north, (p.z ?? 0) - h);
}

/** Moves the camera to a preset. */
function goto(name: string, animate: boolean): Promise<unknown> {
  if (!scene) return Promise.resolve();
  const preset = scene.presets[name];
  if (!preset) return Promise.reject(new Error("unknown preset " + name));
  focus = scene.target;
  setClip(preset.near ? 0 : Infinity);
  return view.goTo(cameraFor(scene, preset), { animate });
}

watch(() => view.updating, (updating) => { state.updating = updating; },
      { initial: true });
// Clip distances follow the camera's distance to the content once it rests.
watch(() => view.stationary, (stationary) => { if (stationary) setClip(cameraDistance()); });
state.clip = () => {
  const c = view.constraints.clipDistance;
  return { mode: c.mode, near: c.near, far: c.far };
};
state.camera = (longitude, latitude, z, tilt) =>
  view.goTo(new Camera({ position: { longitude, latitude, z, spatialReference: SpatialReference.WGS84 },
                         heading: 0, tilt }), { animate: false });

const pass = new MeshPass(view);
state.stats = pass.stats;

let loadSeq = 0;

/** Shows a scene: meshes, test hooks and the panel. */
function showScene(next: Scene) {
  scene = next;
  state.scene = next.name;
  state.anchors = next.anchors;
  pass.setMeshes(next.meshes);
  renderPanel();
}

/**
 * The scene's height model, read from the view at runtime (D28). The view
 * sets heightModelInfo from its elevation layers and keeps it after they are
 * removed, so it counts only while the ground has an elevation layer; without
 * one the ground is the ellipsoid. A freshly added layer reports it once
 * loaded: waited for briefly, else EGM96 is assumed.
 */
async function sceneHeightModel(): Promise<HeightModel> {
  if (!ground.layers.length) return { kind: "ellipsoidal" };
  // SceneView.heightModelInfo exists at runtime but is not in the SDK's
  // typings (5.2.7: only WebScene declares it); missing, the rule applies.
  const viewInfo = () => (view as unknown as
    { heightModelInfo?: { heightModel?: string; vertCRS?: string | null } | null }).heightModelInfo;
  const info = await Promise.race([
    whenOnce(() => viewInfo()).then(() => viewInfo()),
    new Promise<null>((resolve) => setTimeout(() => resolve(null), 5000)),
  ]);
  const { model, note } = heightModelOf(info);
  if (note) loadStatus.add("info", `height model: ${note}`);
  if (!model) {
    loadStatus.add("warn", "height model: the view reports none with terrain; EGM96 assumed");
    return { kind: "gravity" };
  }
  loadStatus.add("info", `height model: ${model.kind}${model.verticalCrs ? ` (${model.verticalCrs})` : ""}`);
  return model;
}

/**
 * Loads the content (the live module with the current layer, recorded data
 * or the W0 spike). Runs at start-up and again for a layer loaded in place;
 * a newer load supersedes one still running.
 */
async function loadContent(): Promise<void> {
  const seq = ++loadSeq;
  state.ready = false;
  state.report = null;
  // Every load after the first starts the status over, also after a failed one.
  if (seq > 1) loadStatus.restart(`loading ${objectLayer ?? "the test scene"}`);
  features.select(null);
  features.solo(null);
  features.mute(null);
  try {
    let next: Scene;
    if (data === "live") {
      const loaded = await loadLiveScene(view, { exact, layer: objectLayer, portal,
                                                 status: loadStatus,
                                                 heightModel: await sceneHeightModel() });
      if (seq !== loadSeq) return;
      state.live = loaded.live;
      next = loaded.scene;
    } else {
      next = data === "w0" ? w0Scene(view, naive) : await loadRecordedScene(view, recordedDir);
    }
    showScene(next);
  } catch (err) {
    if (seq !== loadSeq) return;
    state.errors.push("scene: " + String(err));
    loadStatus.fail(err);
    renderPanel();
    return;
  }
  // A 3D object layer is framed as a whole (its size and spread vary);
  // other content, and a layer without placed meshes, starts at the
  // Overview preset.
  if (!(state.live?.objectLayer && await frameMeshes(null, false))) await goto("overview", false);
  state.goto = (preset) => goto(preset, false);
  state.renderPoint = (mesh, p) => {
    const m = scene?.renderMatrices.get(mesh);
    return m ? transformPoint(m, p) : null;
  };
  state.screenError = (mesh, p) => pass.screenError(mesh, p);
  state.placedBounds = () => {
    const out: Record<string, [number[], number[]]> = {};
    for (const m of scene?.meshes ?? []) {
      const matrix = scene!.renderMatrices.get(m.name);
      if (!matrix) continue;
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      for (let i = 0; i + 2 < m.positions.length; i += 3) {
        const p = transformPoint(matrix, [m.positions[i], m.positions[i + 1], m.positions[i + 2]]);
        for (let k = 0; k < 3; k++) {
          lo[k] = Math.min(lo[k], p[k]);
          hi[k] = Math.max(hi[k], p[k]);
        }
      }
      out[m.name] = [lo, hi];
    }
    return out;
  };
  state.score = () => {
    const r = scene?.scoring ? score(scene.scoring) : null;
    if (r) r.renderer = state.renderer;
    state.report = r;
    renderPanel();
    return r;
  };
  state.select = (path) => features.select(path);
  state.solo = (path) => features.solo(path);
  state.mute = (path, on) => features.mute(path, on);
  state.zoom = (path) => zoomTo(path, false);
  state.ready = true;
  state.loads++;
  loadStatus.finish(`ready: ${scene!.meshes.length} mesh(es) drawn`);
}

/**
 * Loads a 3D object layer in place: the view, map and context layers stay,
 * the URL is updated (history entry) and the layer-dependent defaults of
 * terrain and buildings apply unless the URL sets them. Recorded and
 * spike content switch to live data with a reload.
 */
async function loadLayer(url: string): Promise<void> {
  if (data !== "live") {
    const p = new URLSearchParams(location.search);
    p.set("data", "live");
    if (url) p.set("layer", url); else p.delete("layer");
    location.search = p.toString();
    return;
  }
  objectLayer = url || undefined;
  setParams({ layer: objectLayer ?? null }, true);
  layers.setCurrent(objectLayer);
  terrain = layerDefault("terrain", objectLayer);
  setTerrain(terrain);
  terrainBox.checked = terrain;
  const b = layerDefault("buildings", objectLayer);
  buildingsBox.checked = b;
  showBuildings(b);
  showTab("features");
  await loadContent();
}
state.loadLayer = loadLayer;
// Back and forward through loaded layers: the page state follows the URL.
addEventListener("popstate", () => location.reload());

// The panel ends above the map's attribution bar, whose height changes when
// a visitor expands it (style.css --attribution-height).
view.when(() => {
  const panel = document.getElementById("panel")!;
  const track = () => {
    const bar = view.container?.querySelector<HTMLElement>(".esri-attribution");
    if (!bar) return void requestAnimationFrame(track);
    const update = () => panel.style.setProperty("--attribution-height", `${bar.offsetHeight}px`);
    new ResizeObserver(update).observe(bar);
    update();
  };
  track();
});

view.when(loadContent).catch((err: unknown) => {
  state.errors.push("view: " + String(err));
  loadStatus.fail(err);
});

/** Meshes drawn for an anchor. */
function anchorMeshes(path: string): string[] {
  // (Map is the SDK's here.)
  return [...(scene?.details?.meshAnchor.entries() ?? [])]
    .filter(([, anchor]) => anchor === path).map(([mesh]) => mesh);
}

/**
 * Looks at an anchor's meshes from the south, at a distance from their
 * extent (see frameMeshes).
 */
function zoomTo(path: string, animate: boolean): Promise<unknown> {
  return frameMeshes(new Set(anchorMeshes(path)), animate);
}

/**
 * Looks at meshes (all with null) from the south, at a distance from their
 * extent: a sample of vertices through the render matrices gives the centre
 * and radius in render space, mapped back to lon/lat/h by the SDK. Resolves
 * to false if there is nothing to frame.
 */
function frameMeshes(names: Set<string> | null, animate: boolean): Promise<boolean> {
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (const m of scene?.meshes ?? []) {
    const matrix = scene?.renderMatrices.get(m.name);
    if ((names && !names.has(m.name)) || !matrix) continue;
    const step = Math.max(3, 3 * Math.floor(m.positions.length / 3 / 2000));
    for (let i = 0; i + 2 < m.positions.length; i += step) {
      const p = transformPoint(matrix, [m.positions[i], m.positions[i + 1], m.positions[i + 2]]);
      for (let k = 0; k < 3; k++) {
        lo[k] = Math.min(lo[k], p[k]);
        hi[k] = Math.max(hi[k], p[k]);
      }
    }
  }
  if (!Number.isFinite(lo[0])) return Promise.resolve(false);
  const centre = lo.map((v, k) => (v + hi[k]) / 2);
  const radius = Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2;
  const geo = fromRenderCoordinates(view, centre, 0, new Array(3), 0, SpatialReference.WGS84, 1);
  if (!geo) return Promise.resolve(false);
  const d = Math.max(4 * radius, 20);
  focus = [geo[0], geo[1], geo[2]];
  setClip(d);
  // The panel covers the view's right side: move the camera east so the
  // meshes are centred in the part left of it (half the covered share of
  // the view's width at distance d; the camera looks north).
  const panel = document.getElementById("panel")!.getBoundingClientRect();
  const width = view.width || window.innerWidth;
  const covered = Math.max(0, Math.min(panel.width, width - panel.left)) / width;
  const halfWidth = d * Math.tan(((view.camera?.fov ?? 55) * Math.PI) / 360);
  const east = halfWidth * covered;
  const mPerDegLon = M_PER_DEG_LAT * Math.cos((geo[1] * Math.PI) / 180);
  return view.goTo(new Camera({
    position: { longitude: geo[0] + east / mPerDegLon, latitude: geo[1] - (0.8 * d) / M_PER_DEG_LAT,
                z: geo[2] + 0.6 * d, spatialReference: SpatialReference.WGS84 },
    heading: 0, tilt: 53,
  }), { animate }).then(() => true);
}

// --- panel (D27) ---------------------------------------------------------

initPanel();
initAbout();
state.showTab = showTab;
bindLoadStatus(loadStatus);
const features = new FeaturesTab({
  select: (path) => pass.setHighlight(path ? new Set(anchorMeshes(path)) : null),
  solo: (path) => pass.setSolo(path ? new Set(anchorMeshes(path)) : null),
  mute: (paths) => pass.setMuted(new Set([...paths].flatMap(anchorMeshes))),
  zoom: (path) => void zoomTo(path, true),
});
// Development: test layers (the Zurich office layer and the local mocks).
document.getElementById("devLayers")!.append(...TEST_LAYERS.map((l) => {
  const b = document.createElement("button");
  b.className = "btn sm";
  b.type = "button";
  b.textContent = l.title;
  b.title = `${l.url} (${l.features} features, ${l.crs})`;
  b.dataset.url = l.url;
  b.addEventListener("click", () => void loadLayer(l.url));
  return b;
}));

const layers = new LayersTab({
  current: objectLayer,
  portal: portal ?? DEFAULT_PORTAL,
  log: (level, text) => loadStatus.add(level, text),
  load: (url) => void loadLayer(url),
});

function renderPanel() {
  features.render({ global: viewingMode === "global", scene, live: state.live, report: state.report });
}

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

/** A checkbox that reloads the page with a URL parameter. */
function reloadingBox(id: string, checked: boolean, enabled: boolean, param: string,
                      values: [string, string | null]) {
  const box = document.getElementById(id) as HTMLInputElement;
  box.checked = checked;
  box.disabled = !enabled;
  box.addEventListener("change", () => {
    const p = new URLSearchParams(location.search);
    const v = values[box.checked ? 0 : 1];
    if (v === null) p.delete(param); else p.set(param, v);
    location.search = p.toString();
  });
}
reloadingBox("exact", exact, data === "live", "exact", ["1", null]);
reloadingBox("naive", naive, data === "w0", "naive", ["1", null]);

// Terrain switches in place. It sets the height model, so live content is
// placed again in the matching output CRS (D28).
const terrainBox = document.getElementById("terrain") as HTMLInputElement;
terrainBox.checked = terrain;
terrainBox.addEventListener("change", () => {
  terrain = terrainBox.checked;
  setTerrain(terrain);
  setParams({ terrain: terrain ? "1" : "0" });
  if (data === "live") void loadContent();
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
const buildingsBox = document.getElementById("buildings") as HTMLInputElement;
buildingsBox.checked = buildings;
showBuildings(buildings);
buildingsBox.addEventListener("change", () => {
  showBuildings(buildingsBox.checked);
  setParams({ buildings: buildingsBox.checked ? "1" : "0" });
});

bindDiagnostics(() => {
  const cam = view.camera;
  return [
    ["crossOriginIsolated", String(state.crossOriginIsolated), state.crossOriginIsolated ? "t-ok" : "t-err"],
    ["SharedArrayBuffer", String(state.sharedArrayBuffer), state.sharedArrayBuffer ? "t-ok" : "t-err"],
    ["WebGL renderer", state.renderer],
    ["content", state.scene || "loading"],
    ["engine", state.live
      ? `${state.live.anchors.find((a) => a.engine)?.engine ?? state.live.info.engine} in the browser ` +
        `(engines: ${state.live.info.engines.join(", ")})`
      : scene?.details?.engine ?? "none"],
    ["grids fetched", state.live ? state.live.grids.join(", ") || "none needed" : "not applicable"],
    ["frames", pass.stats.frames.toLocaleString("en")],
    ["meshes / tris", `${pass.stats.meshes} / ${pass.stats.triangles.toLocaleString("en")}`],
    ["near clip", `${pass.stats.near.toFixed(2)} m (${view.constraints.clipDistance.mode})`],
    ["camera z", `${cam?.position.z?.toFixed(2) ?? "-"} m`],
    ...(state.errors.length ? [["errors", state.errors.join("; "), "t-err"] as [string, string, string]] : []),
  ];
});
renderPanel();
