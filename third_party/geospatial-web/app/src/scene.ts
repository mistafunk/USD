// What main.ts draws: placed meshes, a geographic target for the camera
// presets, and per-anchor status for the panel.

import type SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import type SceneView from "@arcgis/core/views/SceneView.js";
import type { PlacedMesh } from "./meshPass";
import type { AnchorPlacement } from "./render";

/** Camera offset from the scene target, in metres east/north/up. */
export interface CameraPreset {
  label: string;
  east: number;
  north: number;
  up: number;
  heading: number;
  tilt: number;
  /** Fixed 10 cm near plane instead of the SDK's automatic clip distances. */
  near?: boolean;
}

export interface AnchorStatus {
  path: string;
  status: string;
  detail: string;
}

/** Placement details for the panel's Features tab (D27). */
export interface SceneDetails {
  output: { name: string; crs: string; frame: "enu" | "crs" };
  engine: string;
  anchors: AnchorPlacement[];
  /** Mesh path -> anchor path, for drawn meshes. */
  meshAnchor: Map<string, string>;
  testCases: TestCase[];
}

export interface Scene {
  name: string;
  /** lon, lat, ellipsoidal height of the camera presets' reference point. */
  target: [number, number, number];
  presets: Record<string, CameraPreset>;
  meshes: PlacedMesh[];
  anchors: AnchorStatus[];
  /** Full Float64 render matrix per mesh name (for numeric checks). */
  renderMatrices: Map<string, Float64Array>;
  /** W2 harness input (control points, test cases, anchor frames), when the
   *  data has them; main.ts scores it. */
  scoring?: HarnessInput;
  /** Placed data (recorded and live scenes; not the W0 spike). */
  details?: SceneDetails;
}

// --- W2 harness data (control points, test cases, scores); checks/harness.ts
// scores them, the rest of the app only carries them. ---

export type Outcome = "pass" | "fail" | "unsupported";

export interface ControlPoint {
  path: string;
  anchor: string | null;
  local: number[] | null;
  reference: { crs: string; position: number[] };
}

export interface TestCase {
  anchor: string;
  expect: Outcome;
  description: string;
  /** Outcome in a local view with matrix placement, where it differs (T5). */
  expectLocal?: Outcome;
  /** Outcome placed live in the browser, where it differs (grids, D23). */
  expectBrowser?: Outcome;
}

export interface HarnessAnchor {
  path: string;
  status: string;
  reason?: string;
  matrix?: number[];
  engine?: string;
  operation?: string;
  accuracy?: number | null;
  skippedOperations?: string[];
}

export interface PointResult {
  name: string;
  error: number | null;
  horizontal: number | null;
  vertical: number | null;
  note?: string;
}

export interface CaseResult {
  anchor: string;
  description: string;
  expect: Outcome;
  result: Outcome;
  ok: boolean;
  detail: string;
  points: PointResult[];
  /** What placed it (T2, T3, T11): engine, operation, accuracy (m, null if
   *  unknown) and more accurate operations skipped for missing grids. */
  engine?: string;
  operation?: string;
  accuracy?: number | null;
  skippedOperations?: string[];
}

export interface HarnessReport {
  view: string;
  output: string;
  engine: string;
  /** "matrix": control points placed with the anchor matrix, as drawn by
   *  default; "exact": with the exact point map (measurement mode, T5). */
  mode: "matrix" | "exact";
  /** WebGL renderer the scores were taken with (set by the app). */
  renderer?: string;
  tolerance: number;
  time: string;
  cases: CaseResult[];
  summary: Record<Outcome | "unexpected", number>;
}

export interface HarnessInput {
  view: SceneView;
  output: string;
  engine: string;
  anchors: HarnessAnchor[];
  controlPoints: ControlPoint[];
  testCases: TestCase[];
  /** Render frame of an anchor, as used for drawing. */
  anchorFrame(path: string): Float64Array | null;
  /** Exact mode: a control point's exact position in the output CRS, and
   *  that CRS's spatial reference in the view. */
  exactPoint?: (controlPoint: string) => number[] | null;
  outputSpatialReference?: SpatialReference;
  /** Placed live in the browser (expectBrowser applies). */
  live?: boolean;
  tolerance: number;
}
