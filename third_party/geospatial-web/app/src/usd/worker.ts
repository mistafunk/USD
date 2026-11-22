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

// PROJ in the module reads proj.db (fetched at startup, D26) and grid files
// (fetched on demand, D23) from here.
const GRID_DIR = "/proj";
const fetchedGrids = new Set<string>();

async function init(moduleUrl: string, projDbUrl: string) {
  const { default: create } = await import(/* @vite-ignore */ moduleUrl);
  // PROJ reads PROJ_DATA once it starts, so set it before the module runs.
  // USD's messages (errors, warnings) go to the page's log.
  module = await create({
    preRun: [(m: Module) => { m.ENV.PROJ_DATA = GRID_DIR; }],
    print: (text: string) => post({ log: { level: "usd", text } }),
    printErr: (text: string) => post({ log: { level: "usd", text } }),
  });
  need(module).FS.mkdir(GRID_DIR);
  // Before anything creates a PROJ context.
  await fetchFiles([{ url: projDbUrl, path: `${GRID_DIR}/proj.db` }]);
  scene = new module.GeospatialScene();
  return module.info();
}

async function load(files: FileSpec[], stage: string) {
  await fetchFiles(files);
  open(stage);
}

function post(message: object) {
  (self as unknown as Worker).postMessage(message);
}

/** Downloads a file, reporting progress (label: the file's path). */
async function download(f: FileSpec): Promise<Uint8Array> {
  let res: Response;
  try {
    res = await fetch(f.url);
  } catch (e) {
    // CORS and network errors only say "Failed to fetch".
    throw new Error(`${f.url}: ${String((e as Error)?.message ?? e)} (network, CORS or COEP)`);
  }
  if (!res.ok) throw new Error(`${f.url}: HTTP ${res.status}`);
  // Progress total: the expected size if given, else Content-Length, which
  // is the size on the wire: with Content-Encoding (gzip, br) the stream
  // delivers more bytes than that, so the total is then unknown. Cross-origin
  // responses may hide Content-Encoding; the loop below catches those.
  const encoding = res.headers.get("content-encoding");
  let total = f.size ?? (encoding && encoding !== "identity"
    ? 0 : Number(res.headers.get("content-length") ?? 0));
  const label = f.path.slice(f.path.lastIndexOf("/") + 1);
  if (!res.body) {
    const data = new Uint8Array(await res.arrayBuffer());
    post({ progress: { label, loaded: data.length, total: data.length } });
    return data;
  }
  const reader = res.body.getReader();
  const chunks: Uint8Array[] = [];
  let loaded = 0, last = 0;
  post({ progress: { label, loaded, total } });
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    loaded += value.length;
    // More than announced: a compressed transfer; the total is unknown.
    if (total && loaded > total) total = 0;
    if (loaded - last > 256 * 1024) {
      post({ progress: { label, loaded, total } });
      last = loaded;
    }
  }
  post({ progress: { label, loaded, total: total || loaded } });
  const data = new Uint8Array(loaded);
  let offset = 0;
  for (const c of chunks) {
    data.set(c, offset);
    offset += c.length;
  }
  return data;
}

/** Fetches files in parallel into the in-memory file system. */
async function fetchFiles(files: FileSpec[]) {
  const blobs = await Promise.all(files.map(download));
  files.forEach((f, i) => writeFile(f.path, blobs[i]));
}

function writeFile(path: string, data: Uint8Array | string) {
  const m = need(module);
  mkdirs(m, path.slice(0, path.lastIndexOf("/")));
  m.FS.writeFile(path, data);
}

/**
 * Fetches the grids from `baseUrl` (a directory with index.json listing the
 * served grids) that are in `names` and not fetched yet, into PROJ's search
 * path, and resets the engines so they see them. Returns the grids fetched.
 */
async function ensureGrids(baseUrl: string, names: string[]): Promise<string[]> {
  const res = await fetch(`${baseUrl}/index.json`);
  if (!res.ok) return [];
  const served = new Set<string>(((await res.json()) as { grids: string[] }).grids);
  const wanted = [...new Set(names)].filter((n) => served.has(n) && !fetchedGrids.has(n));
  if (!wanted.length) return [];
  await fetchFiles(wanted.map((n) => ({ url: `${baseUrl}/${n}`, path: `${GRID_DIR}/${n}` })));
  wanted.forEach((n) => fetchedGrids.add(n));
  need(module).resetEngines();
  return wanted;
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
      case "init": result = await init(args.moduleUrl, args.projDbUrl); break;
      case "load": result = await load(args.files, args.stage); break;
      case "fetchFiles": result = await fetchFiles(args.files); break;
      case "writeText": result = writeFile(args.path, args.text); break;
      case "open": result = open(args.stage); break;
      case "crsWkt": result = need(module).crsWkt(args.definition, args.promote3D); break;
      case "describeCrs": result = need(module).describeCrs(args.definition); break;
      case "projectionFactors": result = need(module).projectionFactors(args.definition, args.positions); break;
      case "anchorCrs": result = need(scene).anchorCrs(args.paths); break;
      case "ensureGrids": result = await ensureGrids(args.baseUrl, args.names); break;
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
