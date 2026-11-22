// What main.ts draws: placed meshes, a geographic target for the camera
// presets, and per-anchor status for the panel.

import type { HarnessReport, TestCase } from "./checks/harness";
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
  /** W2 harness: scores control points, when the data has them. */
  score?: () => HarnessReport;
  /** Placed data (recorded and live scenes; not the W0 spike). */
  details?: SceneDetails;
}
