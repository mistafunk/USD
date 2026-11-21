// W0 spike: a RenderNode draws hard-coded meshes at float32-exact render
// origins, in a global or local SceneView, on a cross-origin isolated page.
//
//   ?view=global|local   viewing mode (default global)
//   &naive=1             bake absolute render coordinates into float32
//                        vertices instead (shows the jitter we avoid)

import esriConfig from "@arcgis/core/config.js";
import Basemap from "@arcgis/core/Basemap.js";
import Camera from "@arcgis/core/Camera.js";
import Ground from "@arcgis/core/Ground.js";
import Map from "@arcgis/core/Map.js";
import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import TileLayer from "@arcgis/core/layers/TileLayer.js";
import SceneView from "@arcgis/core/views/SceneView.js";
import { watch } from "@arcgis/core/core/reactiveUtils.js";
import { renderCoordinateTransformAt } from "@arcgis/core/views/3d/webgl.js";
import { identity } from "./mat4";
import { MeshPass, type PassStats, type PlacedMesh } from "./meshPass";
import { box, type LocalMesh } from "./shapes";

// Self-hosted SDK assets (see vite.config.ts); the CDN worker is blocked by COEP.
esriConfig.assetsPath = __ARCGIS_ASSETS__;

const params = new URLSearchParams(location.search);
const viewingMode = params.get("view") === "local" ? "local" : "global";
const naive = params.get("naive") === "1";

// Zurich main station square; height 0 (no elevation layer in W0).
const ANCHOR = { lon: 8.5402, lat: 47.3779, h: 0 };
const M_PER_DEG_LAT = 111320;
const M_PER_DEG_LON = M_PER_DEG_LAT * Math.cos((ANCHOR.lat * Math.PI) / 180);

interface W0State {
  ready: boolean;
  updating: boolean;
  goto?: (preset: string) => Promise<unknown>;
  viewingMode: string;
  naive: boolean;
  crossOriginIsolated: boolean;
  sharedArrayBuffer: boolean;
  stats: PassStats | null;
  origins: Record<string, number[]>;
  errors: string[];
}

const state: W0State = {
  ready: false,
  updating: true,
  viewingMode,
  naive,
  crossOriginIsolated: globalThis.crossOriginIsolated === true,
  sharedArrayBuffer: typeof SharedArrayBuffer !== "undefined",
  stats: null,
  origins: {},
  errors: [],
};
(globalThis as unknown as { __w0: W0State }).__w0 = state;
addEventListener("error", (e) => state.errors.push(String(e.message)));
addEventListener("unhandledrejection", (e) => state.errors.push(String(e.reason)));

// Keyless Esri imagery tiles (Web Mercator tiling, so it works in both modes).
const basemap = new Basemap({
  baseLayers: [new TileLayer({
    url: "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer",
  })],
});

const view = new SceneView({
  container: "viewDiv",
  // No terrain in W0; let the camera go down to street level (the default
  // stay-above constraint keeps it several metres up).
  map: new Map({ basemap, ground: new Ground({ navigationConstraint: { type: "none" } }) }),
  viewingMode,
  spatialReference: viewingMode === "local" ? SpatialReference.WebMercator : undefined,
  camera: cameraFor("overview"),
});

/**
 * Moves the camera. The SDK picks near/far clip distances from the scene it
 * knows about, which excludes RenderNode geometry, so close to our meshes it
 * clips them. Use a fixed 10 cm near plane for the street-level presets.
 */
function goto(preset: string, animate: boolean): Promise<unknown> {
  const clip = view.constraints.clipDistance;
  if (preset === "overview") {
    clip.mode = "auto";
  } else {
    clip.near = 0.1;
    clip.far = 50_000;
  }
  return view.goTo(cameraFor(preset), { animate });
}

/** Camera at an offset (metres east, north, up) from the anchor. */
function cameraFor(preset: string): Camera {
  const at = (east: number, north: number, up: number, heading: number, tilt: number) =>
    new Camera({
      position: {
        longitude: ANCHOR.lon + east / M_PER_DEG_LON,
        latitude: ANCHOR.lat + north / M_PER_DEG_LAT,
        z: ANCHOR.h + up,
        spatialReference: SpatialReference.WGS84,
      },
      heading,
      tilt,
    });
  switch (preset) {
    case "street": return at(0, -40, 2, 0, 88);
    // 1 m south of the 5 cm pole, looking north at it; the 10 m box's
    // south-west edge is in the background, 4.5 m further.
    case "close": return at(-3, -6.5, 1.5, 0, 90);
    default: return at(0, -500, 350, 0, 55);
  }
}

/**
 * Places a mesh given in local ENU metres at a geographic anchor. The render
 * frame comes from the SDK in Float64; its translation is split into a
 * float32-exact origin plus a small Float64 remainder kept in the matrix.
 */
function place(name: string, local: LocalMesh, east: number, north: number,
               color: [number, number, number]): PlacedMesh {
  const anchor = [
    ANCHOR.lon + east / M_PER_DEG_LON,
    ANCHOR.lat + north / M_PER_DEG_LAT,
    ANCHOR.h,
  ];
  const frame = renderCoordinateTransformAt(view, anchor, SpatialReference.WGS84,
                                            new Float64Array(16));
  if (!frame) throw new Error("renderCoordinateTransformAt failed for " + name);

  if (naive) {
    // What a float32-only pipeline does: absolute render coordinates in the
    // vertex buffer. Large values (ECEF ~6.4e6 m) lose centimetres to metres.
    const positions = new Float32Array(local.positions.length);
    const normals = new Float32Array(local.normals.length);
    for (let i = 0; i < local.positions.length; i += 3) {
      for (let r = 0; r < 3; r++) {
        positions[i + r] = frame[r] * local.positions[i] + frame[4 + r] * local.positions[i + 1] +
          frame[8 + r] * local.positions[i + 2] + frame[12 + r];
        normals[i + r] = frame[r] * local.normals[i] + frame[4 + r] * local.normals[i + 1] +
          frame[8 + r] * local.normals[i + 2];
      }
    }
    state.origins[name] = [0, 0, 0];
    return { name, origin: new Float32Array(3), matrix: identity(), positions, normals,
             indices: local.indices, color };
  }

  const origin = Float32Array.from([frame[12], frame[13], frame[14]]);
  const matrix = Float64Array.from(frame);
  matrix[12] -= origin[0];
  matrix[13] -= origin[1];
  matrix[14] -= origin[2];
  state.origins[name] = Array.from(origin);
  return { name, origin, matrix, positions: local.positions, normals: local.normals,
           indices: local.indices, color };
}

watch(() => view.updating, (updating) => { state.updating = updating; },
      { initial: true });

const pass = new MeshPass(view);
state.stats = pass.stats;

view.when(() => {
  pass.setMeshes([
    place("box10m", box(10, 10, 10), 0, 0, [0.9, 0.45, 0.1]),
    place("box1m", box(1, 1, 1), 8, -3, [0.15, 0.55, 0.9]),
    // A 5 cm pole: wobbles visibly if precision is lost.
    place("pole5cm", box(0.05, 0.05, 4), -3, -5.5, [0.95, 0.95, 0.2]),
  ]);
  state.goto = (preset) => goto(preset, false);
  state.ready = true;
}, (err: unknown) => state.errors.push("view: " + String(err)));

// --- panel ---------------------------------------------------------------

document.querySelectorAll<HTMLAnchorElement>("a[data-view]").forEach((a) => {
  const p = new URLSearchParams(location.search);
  p.set("view", a.dataset.view!);
  a.href = "?" + p.toString();
  a.classList.toggle("active", a.dataset.view === viewingMode);
});
const naiveBox = document.getElementById("naive") as HTMLInputElement;
naiveBox.checked = naive;
naiveBox.addEventListener("change", () => {
  const p = new URLSearchParams(location.search);
  if (naiveBox.checked) p.set("naive", "1"); else p.delete("naive");
  location.search = p.toString();
});
document.querySelectorAll<HTMLButtonElement>("button[data-goto]").forEach((b) =>
  b.addEventListener("click", () => goto(b.dataset.goto!, true)));

const statusEl = document.getElementById("status")!;
setInterval(() => {
  const cam = view.camera;
  statusEl.textContent = [
    `viewingMode       ${viewingMode}${naive ? " (naive float32)" : ""}`,
    `crossOriginIsolated ${state.crossOriginIsolated}`,
    `SharedArrayBuffer ${state.sharedArrayBuffer}`,
    `near ${pass.stats.near.toFixed(2)} m  clip ${view.constraints.clipDistance.mode}`,
    `frames ${pass.stats.frames}  meshes ${pass.stats.meshes}  tris ${pass.stats.triangles}`,
    `camera z ${cam?.position.z?.toFixed(2) ?? "-"} m`,
    `origin box10m ${(state.origins.box10m ?? []).map((v) => v.toFixed(1)).join(", ")}`,
    state.errors.length ? `errors: ${state.errors.join("; ")}` : "",
  ].filter(Boolean).join("\n");
}, 250);

