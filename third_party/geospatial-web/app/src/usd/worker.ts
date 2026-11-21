// Web Worker that owns the OpenUSD module and its stage (PLAN, data flow 1).
// The module's C++ calls are synchronous; running them here keeps the main
// thread and the SceneView responsive. Messages: { id, op, args } in,
// { id, result } or { id, error } out (see client.ts).
import type { FileSpec, SceneState } from "./types";

/* eslint-disable @typescript-eslint/no-explicit-any */
type Module = any;
type Scene = any;

let module: Module | null = null;
let scene: Scene | null = null;

async function init(moduleUrl: string) {
  const { default: create } = await import(/* @vite-ignore */ moduleUrl);
  module = await create();
  scene = new module.GeospatialScene();
  return module.info();
}

async function load(files: FileSpec[], stage: string) {
  await fetchFiles(files);
  open(stage);
}

/** Fetches files in parallel into the in-memory file system. */
async function fetchFiles(files: FileSpec[]) {
  const m = need(module);
  // Fetch in parallel, then write into the in-memory file system.
  const blobs = await Promise.all(files.map(async (f) => {
    const res = await fetch(f.url);
    if (!res.ok) throw new Error(`${f.url}: HTTP ${res.status}`);
    return new Uint8Array(await res.arrayBuffer());
  }));
  files.forEach((f, i) => writeFile(f.path, blobs[i]));
}

function writeFile(path: string, data: Uint8Array | string) {
  const m = need(module);
  mkdirs(m, path.slice(0, path.lastIndexOf("/")));
  m.FS.writeFile(path, data);
}

function open(stage: string) {
  const error = need(scene).open(stage);
  if (error) throw new Error(error);
}

function mkdirs(m: Module, dir: string) {
  let path = "";
  for (const part of dir.split("/").filter(Boolean)) {
    path += "/" + part;
    if (!m.FS.analyzePath(path).exists) m.FS.mkdir(path);
  }
}

function setOutput(crs: string, measure: boolean, exact: boolean) {
  const error = need(scene).setOutput(crs, measure, exact);
  if (error) throw new Error(error);
}

/** Syncs and copies everything out of module memory (views go stale). */
function state(): { value: SceneState; transfer: Transferable[] } {
  const s = need(scene);
  const changed = s.sync();
  const transfer: Transferable[] = [];
  const meshes = s.meshes().map((mesh: any) => {
    const copy = {
      ...mesh,
      points: mesh.points.slice(),
      normals: mesh.normals.slice(),
      indices: mesh.indices.slice(),
      framePoints: mesh.framePoints ? mesh.framePoints.slice() : null,
    };
    transfer.push(copy.points.buffer, copy.normals.buffer, copy.indices.buffer);
    if (copy.framePoints) transfer.push(copy.framePoints.buffer);
    return copy;
  });
  return {
    value: {
      outputWkt: s.outputWkt(),
      frame: s.frame(),
      metersPerUnit: s.metersPerUnit(),
      upAxis: s.upAxis(),
      changed,
      anchors: s.anchors(),
      meshes,
    },
    transfer,
  };
}

function need<T>(value: T | null): T {
  if (!value) throw new Error("module not initialized");
  return value;
}

self.onmessage = async (event: MessageEvent) => {
  const { id, op, args } = event.data;
  try {
    let result: unknown;
    let transfer: Transferable[] = [];
    switch (op) {
      case "init": result = await init(args.moduleUrl); break;
      case "load": result = await load(args.files, args.stage); break;
      case "fetchFiles": result = await fetchFiles(args.files); break;
      case "writeText": result = writeFile(args.path, args.text); break;
      case "open": result = open(args.stage); break;
      case "crsWkt": result = need(module).crsWkt(args.definition, args.promote3D); break;
      case "setOutput": result = setOutput(args.crs, args.measure, args.exact); break;
      case "testData": result = need(scene).testData(); break;
      case "setTime": result = need(scene).setTime(args.time); break;
      case "state": ({ value: result, transfer } = state()); break;
      case "resolve":
        result = need(scene).resolve(args.anchor, args.crs, args.points);
        break;
      default: throw new Error(`unknown op ${op}`);
    }
    (self as unknown as Worker).postMessage({ id, result }, transfer);
  } catch (e) {
    (self as unknown as Worker).postMessage({ id, error: String((e as Error)?.message ?? e) });
  }
};
