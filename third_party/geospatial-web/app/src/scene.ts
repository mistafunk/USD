// What main.ts draws: placed meshes, a geographic target for the camera
// presets, and per-anchor status for the panel.

import type { HarnessReport } from "./harness";
import type { PlacedMesh } from "./meshPass";

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
}
