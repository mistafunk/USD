// Main-thread side of the OpenUSD worker: promise-based calls.
import type { CrsDescription, FileSpec, ModuleInfo, Resolved, SceneState, StageTestData, Vec3 } from "./types";

/** Where the dev server (vite.config.ts) and nginx serve the install. */
export const USD_BASE = "/usd";
export const MODULE_URL = `${USD_BASE}/module/geospatialModule.js`;

export interface UsdProgress {
  label: string;
  loaded: number;
  total: number;
}

export class UsdClient {
  /** Download progress of fetchFiles/load/ensureGrids. */
  onProgress: ((p: UsdProgress) => void) | null = null;
  /** The module's own output (USD errors and warnings). */
  onLog: ((level: "usd", text: string) => void) | null = null;
  private worker = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
  private next = 0;
  private pending = new Map<number, { resolve(v: unknown): void; reject(e: Error): void }>();

  constructor() {
    this.worker.onmessage = (event: MessageEvent) => {
      if (event.data.progress) {
        this.onProgress?.(event.data.progress);
        return;
      }
      if (event.data.log) {
        this.onLog?.(event.data.log.level, event.data.log.text);
        return;
      }
      const { id, result, error } = event.data;
      const p = this.pending.get(id);
      if (!p) return;
      this.pending.delete(id);
      if (error !== undefined) p.reject(new Error(error));
      else p.resolve(result);
    };
    this.worker.onerror = (event) => {
      for (const p of this.pending.values()) p.reject(new Error(event.message || "worker error"));
      this.pending.clear();
    };
  }

  private call<T>(op: string, args: object = {}): Promise<T> {
    const id = this.next++;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (v: unknown) => void, reject });
      this.worker.postMessage({ id, op, args });
    });
  }

  /** Loads the module and PROJ's database (proj.db, D26). */
  init(moduleUrl = MODULE_URL, projDbUrl = `${USD_BASE}/proj/proj.db`): Promise<ModuleInfo> {
    return this.call("init", { moduleUrl, projDbUrl });
  }

  /** Fetches files into the module's file system. */
  fetchFiles(files: FileSpec[]): Promise<void> {
    return this.call("fetchFiles", { files });
  }

  /** Writes a text file (e.g. a generated layer) into the module's file system. */
  writeText(path: string, text: string): Promise<void> {
    return this.call("writeText", { path, text });
  }

  /** Opens a stage from the module's file system. */
  open(stage: string): Promise<void> {
    return this.call("open", { stage });
  }

  /**
   * Fetches those of `names` that the server provides (baseUrl/index.json)
   * into PROJ's grid directory and resets the engines; returns the grids
   * fetched. Set the output again afterwards to place with them.
   */
  ensureGrids(names: string[], baseUrl = `${USD_BASE}/grids`): Promise<string[]> {
    return this.call("ensureGrids", { names, baseUrl });
  }

  /** WKT2 of a CRS definition PROJ accepts, e.g. "EPSG:4326+5773"; with
   *  promote3D a 2D CRS gets an ellipsoidal height axis. */
  crsWkt(definition: string, promote3D = false): Promise<{ wkt: string; error: string }> {
    return this.call("crsWkt", { definition, promote3D });
  }

  /** A CRS definition (WKT, "EPSG:..."), described for diagnostics (D27). */
  describeCrs(definition: string): Promise<CrsDescription> {
    return this.call("describeCrs", { definition });
  }

  /** The source CRS of each anchor, from its composed crs:wkt (D27). */
  anchorCrs(paths: string[]): Promise<Record<string, CrsDescription>> {
    return this.call("anchorCrs", { paths });
  }

  /** Fetches files into the module's file system and opens the stage. */
  load(files: FileSpec[], stage: string): Promise<void> {
    return this.call("load", { files, stage });
  }

  /**
   * EPSG:4979 for a global view, the view's spatial reference for a local one.
   * `measure` adds each mesh's linearization error, `exact` its exact frame
   * points (measurement mode).
   */
  setOutput(crs: string, measure = false, exact = false): Promise<void> {
    return this.call("setOutput", { crs, measure, exact });
  }

  /** Control points, test cases and evaluation time on the stage. */
  testData(): Promise<StageTestData> {
    return this.call("testData");
  }

  setTime(time: number): Promise<void> {
    return this.call("setTime", { time });
  }

  /** Applies pending changes and returns anchors and meshes (buffers transferred). */
  state(): Promise<SceneState> {
    return this.call("state");
  }

  /** The C1 placement query on the stage, with exact output points. */
  resolve(anchor: string, crs: string, points: Vec3[] = []): Promise<Resolved> {
    return this.call("resolve", { anchor, crs, points });
  }

  terminate() {
    this.worker.terminate();
  }
}
