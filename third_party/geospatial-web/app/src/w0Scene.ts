// W0 spike content: hard-coded meshes at float32-exact render origins in
// Zurich. `naive` bakes absolute render coordinates into float32 vertices
// instead, to show the jitter the origin split avoids.

import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import type SceneView from "@arcgis/core/views/SceneView.js";
import { renderCoordinateTransformAt } from "@arcgis/core/views/3d/webgl.js";
import { identity, splitOrigin } from "./mat4";
import type { PlacedMesh } from "./meshPass";
import type { CameraPreset, Scene } from "./scene";
import { box, type LocalMesh } from "./shapes";

// Zurich main station square; height 0 (no elevation layer).
const ANCHOR: [number, number, number] = [8.5402, 47.3779, 0];
const M_PER_DEG_LAT = 111320;
const M_PER_DEG_LON = M_PER_DEG_LAT * Math.cos((ANCHOR[1] * Math.PI) / 180);

const PRESETS: Record<string, CameraPreset> = {
  overview: { label: "Overview", east: 0, north: -500, up: 350, heading: 0, tilt: 55 },
  street: { label: "Street level", east: 0, north: -40, up: 2, heading: 0, tilt: 88, near: true },
  // 1 m south of the 5 cm pole, looking north at it; the 10 m box's
  // south-west edge is in the background, 4.5 m further.
  close: { label: "1 m", east: -3, north: -6.5, up: 1.5, heading: 0, tilt: 90, near: true },
};

export function w0Scene(view: SceneView, naive: boolean): Scene {
  const renderMatrices = new Map<string, Float64Array>();

  /** Places a mesh given in local ENU metres at an offset from the anchor. */
  const place = (name: string, local: LocalMesh, east: number, north: number,
                 color: [number, number, number]): PlacedMesh => {
    const anchor = [ANCHOR[0] + east / M_PER_DEG_LON, ANCHOR[1] + north / M_PER_DEG_LAT,
                    ANCHOR[2]];
    const frame = renderCoordinateTransformAt(view, anchor, SpatialReference.WGS84,
                                              new Float64Array(16));
    if (!frame) throw new Error("renderCoordinateTransformAt failed for " + name);
    renderMatrices.set(name, frame);
    if (!naive) {
      return { name, ...splitOrigin(frame), positions: local.positions,
               normals: local.normals, indices: local.indices, color };
    }
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
    return { name, origin: new Float32Array(3), matrix: identity(), positions, normals,
             indices: local.indices, color };
  };

  return {
    name: naive ? "W0 (naive float32)" : "W0",
    target: ANCHOR,
    presets: PRESETS,
    meshes: [
      place("box10m", box(10, 10, 10), 0, 0, [0.9, 0.45, 0.1]),
      place("box1m", box(1, 1, 1), 8, -3, [0.15, 0.55, 0.9]),
      // A 5 cm pole: wobbles visibly if precision is lost.
      place("pole5cm", box(0.05, 0.05, 4), -3, -5.5, [0.95, 0.95, 0.2]),
    ],
    anchors: [],
    renderMatrices,
  };
}
