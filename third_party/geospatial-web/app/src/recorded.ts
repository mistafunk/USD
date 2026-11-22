// Recorded data from the offline reference (W1), in the layout the C3 scene
// index observer produces (docs/RECORDED_FORMAT.md): a manifest and its
// buffers, placed like live data (render.ts).

import type SceneView from "@arcgis/core/views/SceneView.js";
import type { ControlPoint, TestCase } from "./harness";
import type { Scene } from "./scene";
import { placeScene, type AnchorPlacement } from "./render";

interface BufferView {
  byteOffset: number;
  count: number;
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
  /** The reference names pyproj and its PROJ; the C++ observer only the engine. */
  engine: { name: string; version?: string; proj?: string };
  buffers: string;
  anchors: AnchorPlacement[];
  meshes: RecordedMesh[];
  /** Test data (W2): control points and expected outcomes per anchor. */
  controlPoints?: ControlPoint[];
  testCases?: TestCase[];
}

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
  return placeScene(view, {
    name: `recorded ${manifest.output.name} (${engineLabel(manifest)})`,
    output: manifest.output,
    engine: engineLabel(manifest),
    anchors: manifest.anchors,
    meshes: manifest.meshes.map((m) => ({
      ...m,
      positions: new Float32Array(buffers, m.points.byteOffset, m.points.count * 3),
      normals: new Float32Array(buffers, m.normals.byteOffset, m.normals.count * 3),
      indices: new Uint32Array(buffers, m.indices.byteOffset, m.indices.count),
    })),
    controlPoints: manifest.controlPoints ?? [],
    testCases: manifest.testCases ?? [],
  });
}

function engineLabel(manifest: RecordedManifest): string {
  const { name, version, proj } = manifest.engine;
  return [version ? `${name} ${version}` : name, proj && `PROJ ${proj}`]
    .filter(Boolean).join(", ");
}

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: ${res.status}`);
  return res.json() as Promise<T>;
}
