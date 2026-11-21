// Geospatial web app: a RenderNode draws placed meshes in a global or local
// SceneView on a cross-origin isolated page.
//
//   ?view=global|local   viewing mode (default global)
//   &data=recorded|w0    recorded reference export (W1, default) or the W0
//                        hard-coded meshes
//   &naive=1             W0 only: bake absolute render coordinates into
//                        float32 vertices (shows the jitter we avoid)

import esriConfig from "@arcgis/core/config.js";
import Basemap from "@arcgis/core/Basemap.js";
import Camera from "@arcgis/core/Camera.js";
import Ground from "@arcgis/core/Ground.js";
import Map from "@arcgis/core/Map.js";
import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import TileLayer from "@arcgis/core/layers/TileLayer.js";
import SceneView from "@arcgis/core/views/SceneView.js";
import { watch } from "@arcgis/core/core/reactiveUtils.js";
import type { HarnessReport } from "./harness";
import { transformPoint } from "./mat4";
import { MeshPass, type PassStats } from "./meshPass";
import { loadRecordedScene } from "./recorded";
import type { CameraPreset, Scene } from "./scene";
import { w0Scene } from "./w0Scene";

// Self-hosted SDK assets (see vite.config.ts); the CDN worker is blocked by COEP.
esriConfig.assetsPath = __ARCGIS_ASSETS__;

const params = new URLSearchParams(location.search);
const viewingMode = params.get("view") === "local" ? "local" : "global";
const data = params.get("data") === "w0" ? "w0" : "recorded";
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
  /** W2 harness: control point scoring for the current view. */
  score?: () => HarnessReport | null;
  report: HarnessReport | null;
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

const view = new SceneView({
  container: "viewDiv",
  // No terrain yet; let the camera go down to street level (the default
  // stay-above constraint keeps it several metres up).
  map: new Map({ basemap, ground: new Ground({ navigationConstraint: { type: "none" } }) }),
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
  scene = data === "w0" ? w0Scene(view, naive) : await loadRecordedScene(view, "recorded");
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
  state.score = () => {
    const r = scene?.score?.() ?? null;
    if (r) r.renderer = state.renderer;
    return (state.report = r);
  };
  state.ready = true;
}).catch((err: unknown) => state.errors.push("scene: " + String(err)));

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
