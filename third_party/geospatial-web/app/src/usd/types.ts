// Data the OpenUSD module's worker returns (module/geospatialModule.cpp).

export type Vec3 = [number, number, number];

export interface ModuleInfo {
  engine: string;
  engines: string[];
  threads: number;
}

export interface LiveAnchor {
  path: string;
  status: "ok" | "unsupported" | "error";
  reason: string;
  engine: string;
  operation: string;
  /** Metres; null if unknown. */
  accuracy: number | null;
  /** More accurate operations skipped for missing grids (T3). */
  skippedOperations: string[];
  /** Grid files that would allow a better or any placement (D23). */
  missingGrids: string[];
  sourceCrs: string;
  position: Vec3;
  orientation: Vec3;
  /** The anchor in the output CRS. */
  origin: Vec3;
  /** Anchor model frame to its frame at origin, column-major. */
  matrix: number[];
}

export interface LiveMesh {
  path: string;
  anchor: string | null;
  status: string;
  reason: string;
  color: Vec3;
  /** Mesh-local (stage units) to the anchor's frame, column-major. */
  matrix: number[];
  linearizationError: number | null;
  /** Flat-shaded triangles: xyz triples, indices 0..n-1. */
  points: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
  /** Exact mode: the corners mapped exactly into the anchor's frame. */
  framePoints: Float32Array | null;
}

export interface SceneState {
  /** Output CRS definition in use (a 2D output is promoted to 3D). */
  outputWkt: string;
  /** "enu" for a geographic output, else "crs". */
  frame: "enu" | "crs";
  metersPerUnit: number;
  upAxis: string;
  changed: string[];
  anchors: LiveAnchor[];
  meshes: LiveMesh[];
}

export interface Resolved {
  status: "ok" | "unsupported" | "error";
  reason: string;
  engine: string;
  operation?: string;
  accuracy?: number | null;
  origin?: Vec3;
  matrix?: number[];
  outputPoints?: Vec3[];
  pointsError?: string;
  missingGrids: string[];
}

/** Test data on the stage (geotest: attributes), as in the recorded format. */
export interface StageTestData {
  controlPoints: {
    path: string; anchor: string | null; local: number[] | null;
    reference: { crs: string; position: number[] };
  }[];
  testCases: { anchor: string; expect: "pass" | "fail" | "unsupported";
               description: string; expectLocal?: "pass" | "fail" | "unsupported";
               expectBrowser?: "pass" | "fail" | "unsupported" }[];
  /** The scene's evaluation time (geotest:time), or null. */
  time: number | null;
}

/** A file to fetch into the module's file system. */
export interface FileSpec {
  url: string;
  path: string;
  /** Expected size in bytes, if known (e.g. an ArcGIS asset map's size):
   *  the progress total, also when the response is compressed in transit. */
  size?: number;
}

/** A CRS part with its identifier, e.g. { name: "WGS 84", id: "EPSG:4326" }. */
export interface CrsPart {
  name: string;
  /** "AUTH:code", empty if PROJ knows none (e.g. "ellipsoidal height"). */
  id: string;
}

/** A CRS described by the module for diagnostics (D27). */
export interface CrsDescription {
  /** Empty when the CRS could be read. */
  error: string;
  name?: string;
  id?: string;
  /** "geographic 2D", "geographic 3D", "projected", "compound", ... */
  type?: string;
  dimension?: number;
  wkt?: string;
  horizontal?: CrsPart;
  vertical?: CrsPart;
  /** "EPSG:3855" for a 2D CRS: the proposal's default height (EGM2008). */
  defaultHeight?: string;
  areaOfUse?: { name: string; west: number; south: number; east: number; north: number };
}

/** Map projection factors at a position (engine), or why there are none. */
export interface ProjectionFactors {
  error?: string;
  /** Degrees from true north to grid north, clockwise positive. */
  meridianConvergence?: number;
  /** Grid length over ellipsoid length along the meridian and the parallel. */
  meridionalScale?: number;
  parallelScale?: number;
}
