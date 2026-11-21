// Recorded data from the offline reference (W1), in the layout the C3 scene
// index observer will produce. See docs/RECORDED_FORMAT.md.
//
// Per D13, each mesh carries an anchor in the output CRS and a Float64 matrix
// from mesh-local units into the anchor's frame. JS maps the anchor into
// render space. A local view whose spatial reference is the output CRS just
// translates by the anchor. A global view (EPSG:4979 anchor, ENU metres) uses
// the derivative of the SDK's own geographic-to-render mapping at the anchor:
// render space is a 6378137 m sphere, so the metric frame from
// renderCoordinateTransformAt disagrees with lon/lat-placed content by about
// 0.1-0.3 % (2.4 cm over 22 m in Redlands; W1 finding).

import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import { xyToLngLat } from "@arcgis/core/geometry/support/webMercatorUtils.js";
import type SceneView from "@arcgis/core/views/SceneView.js";
import { toRenderCoordinates } from "@arcgis/core/views/3d/webgl.js";
import { score, type ControlPoint, type TestCase } from "./harness";
import { multiply, splitOrigin, translation } from "./mat4";
import type { PlacedMesh } from "./meshPass";
import type { AnchorStatus, CameraPreset, Scene } from "./scene";

interface BufferView {
  byteOffset: number;
  count: number;
}

interface RecordedAnchor {
  path: string;
  status: "ok" | "unsupported" | "error";
  reason?: string;
  sourceCrs?: string;
  origin?: number[];
  /** Anchor model frame (stage units) to the anchor's frame; Float64. */
  matrix?: number[];
  operation?: string;
  accuracy?: number | null;
}

interface RecordedMesh {
  path: string;
  anchor: string | null;
  status: string;
  reason?: string;
  color?: [number, number, number];
  matrix?: number[];
  points: BufferView;
  normals: BufferView;
  indices: BufferView;
}

export interface RecordedManifest {
  format: string;
  version: number;
  stage: string;
  output: { name: string; crs: string; frame: "enu" | "crs" };
  engine: { name: string; version: string; proj: string };
  buffers: string;
  anchors: RecordedAnchor[];
  meshes: RecordedMesh[];
  /** Test data (W2): control points and expected outcomes per anchor. */
  controlPoints?: ControlPoint[];
  testCases?: TestCase[];
}

const PRESETS: Record<string, CameraPreset> = {
  overview: { label: "Overview", east: 0, north: -260, up: 180, heading: 0, tilt: 55 },
  street: { label: "Street level", east: -10, north: -75, up: 2, heading: 5, tilt: 88, near: true },
  // The mast (Site B, geographic 3D) should stand exactly on the building's
  // north-east corner (Site A, UTM 11N).
  // About 8 m north of the corner, outside the building (model (-2, +8) from
  // the corner, heading 30), looking back at it.
  corner: { label: "Corner", east: 2.27, north: 7.93, up: 3, heading: 196, tilt: 82, near: true },
};

export async function loadRecordedScene(view: SceneView, base: string): Promise<Scene> {
  const output = view.viewingMode === "global" ? "global" : "local-3857";
  const manifest = await fetchJson<RecordedManifest>(`${base}/manifest-${output}.json`);
  if (manifest.format !== "geospatial-web-recorded" || manifest.version !== 1) {
    throw new Error(`unexpected recorded format ${manifest.format} v${manifest.version}`);
  }
  const res = await fetch(`${base}/${manifest.buffers}`);
  if (!res.ok) throw new Error(`${res.url}: ${res.status}`);
  return placeRecorded(view, manifest, await res.arrayBuffer());
}

export function placeRecorded(view: SceneView, manifest: RecordedManifest,
                              buffers: ArrayBuffer): Scene {
  const anchors = new Map(manifest.anchors.map((a) => [a.path, a]));
  if (manifest.output.frame === "crs") {
    const wkid = Number(manifest.output.crs.replace(/^EPSG:/, ""));
    const sr = view.spatialReference;
    // The SDK reports Web Mercator as wkid 102100 (latestWkid 3857).
    if (sr.wkid !== wkid && sr.latestWkid !== wkid) {
      throw new Error(`recorded output ${manifest.output.crs} is not the view's ` +
                      `spatial reference (wkid ${view.spatialReference.wkid})`);
    }
  }

  const frames = new Map<string, Float64Array>();
  const frameOf = (a: RecordedAnchor): Float64Array => {
    const cached = frames.get(a.path);
    if (cached) return cached;
    const f = computeFrame(a);
    frames.set(a.path, f);
    return f;
  };
  const computeFrame = (a: RecordedAnchor): Float64Array => {
    const origin = a.origin!;
    if (manifest.output.frame === "crs") return translation(origin);
    const frame = geographicFrame(view, origin);
    if (!frame) throw new Error(`no render frame at ${a.path}`);
    return frame;
  };

  const meshes: PlacedMesh[] = [];
  const renderMatrices = new Map<string, Float64Array>();
  for (const m of manifest.meshes) {
    const a = m.anchor ? anchors.get(m.anchor) : undefined;
    if (m.status !== "ok" || !a || a.status !== "ok" || !m.matrix) continue;
    const full = multiply(frameOf(a), m.matrix);
    renderMatrices.set(m.path, full);
    meshes.push({
      name: m.path,
      ...splitOrigin(full),
      positions: new Float32Array(buffers, m.points.byteOffset, m.points.count * 3),
      normals: new Float32Array(buffers, m.normals.byteOffset, m.normals.count * 3),
      indices: new Uint32Array(buffers, m.indices.byteOffset, m.indices.count),
      color: m.color ?? [0.8, 0.8, 0.8],
    });
  }

  const status: AnchorStatus[] = manifest.anchors.map((a) => ({
    path: a.path,
    status: a.status,
    detail: a.status === "ok"
      ? `${a.sourceCrs}; ${a.operation}; accuracy ${a.accuracy ?? "unknown"}`
      : a.reason ?? "",
  }));
  for (const m of manifest.meshes) {
    if (m.status === "unplaced") {
      status.push({ path: m.path, status: "unplaced", detail: m.reason ?? "" });
    }
  }

  // Camera presets are relative to Site B's anchor (the building corner).
  const ok = manifest.anchors.filter((a) => a.status === "ok");
  const ref = ok.find((a) => a.path.endsWith("/SiteB")) ?? ok[0];
  if (!ref) throw new Error("no placeable anchor in the recorded data");
  return {
    name: `recorded ${manifest.output.name} (${manifest.engine.name} ` +
          `${manifest.engine.version}, PROJ ${manifest.engine.proj})`,
    target: toLonLatH(manifest, ref.origin!),
    presets: PRESETS,
    meshes,
    anchors: status,
    renderMatrices,
    score: () => score({
      view,
      output: manifest.output.name,
      engine: `${manifest.engine.name} ${manifest.engine.version}, PROJ ${manifest.engine.proj}`,
      anchors: manifest.anchors,
      controlPoints: manifest.controlPoints ?? [],
      testCases: manifest.testCases ?? [],
      anchorFrame: (path) => {
        const a = anchors.get(path);
        return a && a.status === "ok" ? frameOf(a) : null;
      },
      tolerance: 0.01,
    }),
  };
}

const WGS84_A = 6378137;
const WGS84_E2 = 6.69437999014e-3;

/**
 * Render-space frame at a WGS84 lon/lat/height: translation is the anchor's
 * render position, columns are the render-space derivative per metre east,
 * north and up along the ellipsoid, by central differences (+-1 m) through
 * the SDK's own mapping. All Float64.
 */
export function geographicFrame(view: SceneView, [lon, lat, h]: number[]): Float64Array | null {
  const phi = (lat * Math.PI) / 180;
  const w = Math.sqrt(1 - WGS84_E2 * Math.sin(phi) ** 2);
  const n = WGS84_A / w;                       // prime vertical radius
  const m = (WGS84_A * (1 - WGS84_E2)) / w ** 3; // meridian radius
  const dLon = (180 / Math.PI) / ((n + h) * Math.cos(phi));
  const dLat = (180 / Math.PI) / (m + h);
  const src = [
    lon, lat, h,
    lon + dLon, lat, h, lon - dLon, lat, h,
    lon, lat + dLat, h, lon, lat - dLat, h,
    lon, lat, h + 1, lon, lat, h - 1,
  ];
  const out = toRenderCoordinates(view, src, 0, SpatialReference.WGS84,
                                  new Float64Array(src.length), 0, 7);
  if (!out) return null;
  const f = new Float64Array(16);
  for (let axis = 0; axis < 3; axis++) {
    for (let r = 0; r < 3; r++) {
      f[axis * 4 + r] = (out[3 + axis * 6 + r] - out[6 + axis * 6 + r]) / 2;
    }
  }
  f[12] = out[0];
  f[13] = out[1];
  f[14] = out[2];
  f[15] = 1;
  return f;
}

function toLonLatH(manifest: RecordedManifest, origin: number[]): [number, number, number] {
  if (manifest.output.frame === "enu") return [origin[0], origin[1], origin[2]];
  const [lon, lat] = xyToLngLat(origin[0], origin[1]);
  return [lon, lat, origin[2]];
}

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: ${res.status}`);
  return res.json() as Promise<T>;
}
