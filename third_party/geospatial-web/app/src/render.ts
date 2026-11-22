// From placement results to the SDK's render space (D13), for recorded and
// live data: each mesh carries an anchor in the output CRS and a Float64
// matrix from mesh-local units into the anchor's frame; JS maps the anchor
// into render space. A local view whose spatial reference is the output CRS
// just translates by the anchor. A global view (EPSG:4979 anchor, ENU metres)
// uses the derivative of the SDK's own geographic-to-render mapping at the
// anchor: render space is a 6378137 m sphere, so the metric frame from
// renderCoordinateTransformAt disagrees with lon/lat-placed content by about
// 0.1-0.3 % (2.4 cm over 22 m in Redlands; W1 finding).

import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import { xyToLngLat } from "@arcgis/core/geometry/support/webMercatorUtils.js";
import type SceneView from "@arcgis/core/views/SceneView.js";
import { toRenderCoordinates } from "@arcgis/core/views/3d/webgl.js";
import { score, type ControlPoint, type TestCase } from "./harness";
import { identity, multiply, normalMatrix3, splitOrigin, translation } from "./mat4";
import type { PlacedMesh } from "./meshPass";
import type { AnchorStatus, CameraPreset, Scene } from "./scene";

/**
 * One anchor's placement result, recorded (offline reference, W1) or live
 * (the C3 observer): status, the anchor's origin in the output CRS and the
 * matrix from its model frame into the anchor's frame (D13).
 */
export interface AnchorPlacement {
  path: string;
  status: "ok" | "unsupported" | "error";
  reason?: string;
  sourceCrs?: string;
  origin?: number[];
  /** Anchor model frame (stage units) to the anchor's frame; Float64. */
  matrix?: number[];
  engine?: string;
  operation?: string;
  accuracy?: number | null;
  skippedOperations?: string[];
}

const PRESETS: Record<string, CameraPreset> = {
  overview: { label: "Overview", east: 0, north: -260, up: 180, heading: 0, tilt: 55 },
  street: { label: "Street level", east: -10, north: -75, up: 2, heading: 5, tilt: 88, near: true },
  // The mast (Site B, geographic 3D) should stand exactly on the building's
  // north-east corner (Site A, UTM 11N).
  // About 8 m north of the corner, outside the building (model (-2, +8) from
  // the corner, heading 30), looking back at it.
  corner: { label: "Corner", east: 2.27, north: 7.93, up: 3, heading: 196, tilt: 82, near: true },
  // T6: 1 m from the corner (0.6 m north-east, 0.8 m up), looking down at it.
  close: { label: "1 m", east: 0.42, north: 0.42, up: 0.8, heading: 225, tilt: 37, near: true },
};

/** A mesh to place: buffers in mesh-local units, or exact frame points. */
export interface InputMesh {
  path: string;
  anchor: string | null;
  status: string;
  reason?: string;
  color?: number[];
  matrix?: number[];
  positions: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
  /** Exact mode: the corners mapped exactly into the anchor's frame. */
  framePoints?: Float32Array | null;
}

/** What placeScene draws and scores, from recorded or live data. */
export interface PlaceInput {
  name: string;
  output: { name: string; crs: string; frame: "enu" | "crs" };
  engine: string;
  anchors: AnchorPlacement[];
  meshes: InputMesh[];
  controlPoints: ControlPoint[];
  testCases: TestCase[];
  /** Exact mode (measurement mode, T5): meshes drawn from framePoints and
   *  control points scored at these exact output positions. */
  exactPoints?: Map<string, number[]>;
  /** Camera target (lon, lat, h) when no anchor could be placed. */
  fallbackTarget?: [number, number, number];
  /** Placed live in the browser (the harness applies expectBrowser). */
  live?: boolean;
}

export function placeScene(view: SceneView, input: PlaceInput): Scene {
  const exact = !!input.exactPoints;
  const anchors = new Map(input.anchors.map((a) => [a.path, a]));
  if (input.output.frame === "crs") {
    // The horizontal part: a compound output (e.g. EPSG:3857+5773) has the
    // view's CRS with a vertical CRS (D28).
    const wkid = Number(input.output.crs.replace(/^EPSG:/, "").split("+")[0]);
    const sr = view.spatialReference;
    // The SDK reports Web Mercator as wkid 102100 (latestWkid 3857).
    if (sr.wkid !== wkid && sr.latestWkid !== wkid) {
      throw new Error(`output ${input.output.crs} is not the view's ` +
                      `spatial reference (wkid ${view.spatialReference.wkid})`);
    }
  }

  const frames = new Map<string, Float64Array>();
  const frameOf = (a: AnchorPlacement): Float64Array => {
    const cached = frames.get(a.path);
    if (cached) return cached;
    const f = computeFrame(a, a.origin!);
    frames.set(a.path, f);
    return f;
  };
  const computeFrame = (a: AnchorPlacement, origin: number[]): Float64Array => {
    if (input.output.frame === "crs") return translation(origin);
    const frame = geographicFrame(view, origin);
    if (!frame) throw new Error(`no render frame at ${a.path}`);
    return frame;
  };

  const meshes: PlacedMesh[] = [];
  const renderMatrices = new Map<string, Float64Array>();
  for (const m of input.meshes) {
    const a = m.anchor ? anchors.get(m.anchor) : undefined;
    if (m.status !== "ok" || !a || a.status !== "ok" || !m.matrix) continue;
    // Exact mode draws the exact frame points: the matrix is the identity.
    const useExact = exact && !!m.framePoints;
    const full = multiply(frameOf(a), useExact ? identity() : m.matrix);
    renderMatrices.set(m.path, full);
    meshes.push({
      name: m.path,
      ...splitOrigin(full),
      positions: useExact ? m.framePoints! : m.positions,
      normals: useExact ? transformNormals(m.matrix, m.normals) : m.normals,
      indices: m.indices,
      color: (m.color ?? [0.8, 0.8, 0.8]) as [number, number, number],
    });
  }

  const status: AnchorStatus[] = input.anchors.map((a) => ({
    path: a.path,
    status: a.status,
    detail: a.status === "ok"
      ? `${a.sourceCrs}; ${a.operation}; accuracy ${a.accuracy ?? "unknown"}`
      : a.reason ?? "",
  }));
  for (const m of input.meshes) {
    if (m.status === "unplaced") {
      status.push({ path: m.path, status: "unplaced", detail: m.reason ?? "" });
    }
  }

  // Camera presets are relative to Site B's anchor (the building corner).
  const ok = input.anchors.filter((a) => a.status === "ok");
  const ref = ok.find((a) => a.path.endsWith("/SiteB")) ?? ok[0];
  if (!ref && !input.fallbackTarget) throw new Error("no placeable anchor in the data");
  return {
    name: input.name,
    target: ref ? toLonLatH(input.output.frame, ref.origin!) : input.fallbackTarget!,
    presets: PRESETS,
    meshes,
    anchors: status,
    renderMatrices,
    details: {
      output: input.output,
      engine: input.engine,
      anchors: input.anchors,
      meshAnchor: new Map(input.meshes.filter((m) => m.anchor && renderMatrices.has(m.path))
        .map((m) => [m.path, m.anchor!])),
      testCases: input.testCases,
    },
    score: () => score({
      view,
      output: input.output.name,
      engine: input.engine,
      anchors: input.anchors,
      controlPoints: input.controlPoints,
      testCases: input.testCases,
      anchorFrame: (path) => {
        const a = anchors.get(path);
        return a && a.status === "ok" ? frameOf(a) : null;
      },
      ...(exact ? {
        exactPoint: (cp: string) => input.exactPoints!.get(cp) ?? null,
        outputSpatialReference: input.output.frame === "enu"
          ? SpatialReference.WGS84 : view.spatialReference,
      } : {}),
      live: input.live,
      tolerance: 0.01,
    }),
  };
}

/** Normals into the anchor's frame (exact mode draws frame coordinates). */
function transformNormals(matrix: number[], normals: Float32Array): Float32Array {
  const n3 = normalMatrix3(matrix);
  const out = new Float32Array(normals.length);
  for (let i = 0; i < normals.length; i += 3) {
    const [x, y, z] = [normals[i], normals[i + 1], normals[i + 2]];
    const v = [0, 1, 2].map((r) => n3[r] * x + n3[3 + r] * y + n3[6 + r] * z);
    const len = Math.hypot(v[0], v[1], v[2]) || 1;
    out.set(v.map((c) => c / len), i);
  }
  return out;
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

export function toLonLatH(frame: "enu" | "crs", origin: number[]): [number, number, number] {
  if (frame === "enu") return [origin[0], origin[1], origin[2]];
  const [lon, lat] = xyToLngLat(origin[0], origin[1]);
  return [lon, lat, origin[2]];
}
