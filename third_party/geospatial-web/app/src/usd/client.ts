// Main-thread side of the OpenUSD worker: promise-based calls.
import type { FileSpec, ModuleInfo, Resolved, SceneState, Vec3 } from "./types";

/** Where the dev server (vite.config.ts) and nginx serve the install. */
export const USD_BASE = "/usd";
export const MODULE_URL = `${USD_BASE}/module/geospatialModule.js`;

export class UsdClient {
  private worker = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
  private next = 0;
  private pending = new Map<number, { resolve(v: unknown): void; reject(e: Error): void }>();

  constructor() {
    this.worker.onmessage = (event: MessageEvent) => {
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

  init(moduleUrl = MODULE_URL): Promise<ModuleInfo> {
    return this.call("init", { moduleUrl });
  }

  /** Fetches files into the module's file system and opens the stage. */
  load(files: FileSpec[], stage: string): Promise<void> {
    return this.call("load", { files, stage });
  }

  /** EPSG:4979 for a global view, the view's spatial reference for a local one. */
  setOutput(crs: string, measure = false): Promise<void> {
    return this.call("setOutput", { crs, measure });
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
