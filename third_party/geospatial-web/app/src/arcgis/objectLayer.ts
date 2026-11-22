// ArcGIS 3D object feature layers as USD: a wrapper layer that places each
// feature's model (glb, read by the Adobe glTF plugin) with the geospatial
// CRS schemas, from the layer's spatial reference and the feature's
// transform fields (infoFor3D.transformFieldRoles). The models are referenced
// unchanged.
//
// Layer data comes from the feature service REST API (a .../FeatureServer/<id>
// URL: layer resource and query3D) or, for tests, from a directory with the
// same two responses as layer.json and query3D.json (testdata/arcgis).
//
// Transform semantics, as the Maps SDK reads them (verified 2026-11-22 on
// rotated features of the Zurich org's "3D Objekte" layers against the SDK's
// baked mesh vertices, and in its source): the transform fields are in the
// model's glTF frame (x east, y up, z south). The SDK converts them to ENU as
// translation (tX, -tZ, tY), rotation axis (rX, -rZ, rY), scale (sX, sZ, sY),
// angle in degrees right-handed, and places a model point p at
// R(axis, angle) * (scale * p) + translation at the origin. The wrapper's
// axes are that glTF frame, so the fields go into its xformOps unchanged.

export interface SpatialReferenceJson {
  wkid?: number;
  latestWkid?: number;
  vcsWkid?: number;
  latestVcsWkid?: number;
}

export interface ObjectLayerJson {
  name?: string;
  /** Service capabilities, e.g. "Create,Delete,Query,Update". */
  capabilities?: string;
  spatialReference: SpatialReferenceJson;
  infoFor3D: {
    transformFieldRoles: Record<string, string>;
    /** Formats query3D can return, e.g. ["3D_shapebuffer", "3D_glb"]. */
    queryFormats?: string[];
  };
}

export interface AssetMapJson {
  parentGlobalId: string;
  assetName: string;
  assetHash: string;
  assetType: string;
  assetURL: string;
  conversionStatus?: string;
  /** Size of the model in bytes (uncompressed). */
  size?: number;
}

export interface ObjectLayerData {
  url: string;
  layer: ObjectLayerJson;
  features: { attributes: Record<string, unknown> }[];
  assetMaps: AssetMapJson[];
  /** The model format queried and referenced (modelFormat). */
  format: string;
}

/**
 * Model formats the app loads, preferred first: USD, which OpenUSD reads
 * natively, then glTF binary through the glTF plugin. Layers serve only
 * 3D_glb today (USD is an upload format they convert); query formats with
 * USD are expected, and then used. USD models are assumed to follow the glb
 * conversions' convention (Y-up, metres); no correction is applied.
 */
export const MODEL_FORMATS = ["3D_usdz", "3D_usdc", "3D_usd", "3D_usda", "3D_glb"];

/** File extension per model format. */
const EXTENSION: Record<string, string> = {
  "3D_usdz": "usdz", "3D_usdc": "usdc", "3D_usd": "usd", "3D_usda": "usda", "3D_glb": "glb",
};

/** The preferred model format a layer can return (its query formats), else 3D_glb. */
export function modelFormat(queryFormats: string[] | undefined): string {
  return MODEL_FORMATS.find((f) => queryFormats?.includes(f)) ?? "3D_glb";
}

export type Log = (text: string) => void;

/**
 * A layer's display title from its URL: the service name of a feature
 * service ("Zurich_Office_4th_Floor" -> "Zurich Office 4th Floor"), else the
 * last path segment (a mock directory's name).
 */
export function titleFromUrl(url: string): string {
  const service = /\/services\/([^/]+)\/FeatureServer/.exec(url)?.[1];
  const name = service ?? url.replace(/[?#].*$/, "").replace(/\/+$/, "").split("/").pop() ?? url;
  return decodeURIComponent(name).replace(/_/g, " ");
}

/**
 * The 3D object feature layer URL for what a user entered: an ArcGIS item id
 * (32 hex characters, looked up on `portal`), a feature service URL (its
 * first layer with infoFor3D), a layer URL (.../FeatureServer/<id>) or a
 * mock directory (/usd/fixtures/arcgis/<name>); with a title, the item's when
 * there is one, else from the URL (titleFromUrl).
 */
export async function resolveObjectLayerUrl(input: string, portal: string,
                                            log: Log): Promise<{ url: string; title: string }> {
  const withTitle = async (url: string | Promise<string>, title?: string) => {
    const u = await url;
    return { url: u, title: title || titleFromUrl(u) };
  };
  let s = input.trim();
  // An item page (https://<portal>/home/item.html?id=<id>): the item on
  // that portal.
  const page = /^(https?:\/\/[^?#]+?)\/home\/item\.html\?(?:[^#]*&)?id=([0-9a-f]{32})/i.exec(s);
  if (page) {
    portal = page[1];
    s = page[2];
  } else if (/^https?:\/\//.test(s) || s.startsWith("/")) {
    // Service URLs as copied from a browser: without ?f=json or a fragment.
    s = s.replace(/[?#].*$/, "").replace(/\/+$/, "");
    return withTitle(/\/FeatureServer$/.test(s) ? firstObjectLayer(s, log) : s);
  }
  if (!/^[0-9a-f]{32}$/i.test(s)) {
    throw new Error(`"${s}" is neither an ArcGIS item id nor a layer URL`);
  }
  const base = portal.replace(/\/+$/, "");
  const item = await fetchJson<{ title?: string; type?: string; url?: string; owner?: string;
                                 access?: string; typeKeywords?: string[] }>(
    `${base}/sharing/rest/content/items/${s}?f=json`, log);
  log(`item ${s}: "${item.title}" (${item.type}; ${(item.typeKeywords ?? []).join(", ")}), ` +
      `owner ${item.owner}, ${item.access}`);
  if (!item.url) throw new Error(`item ${s} has no service URL (type ${item.type})`);
  if (!/\/FeatureServer/.test(item.url)) {
    throw new Error(`item ${s} is a ${item.type} (${item.url}), not a feature service: ` +
                    "3D object layers are read from their feature layer");
  }
  const url = item.url.replace(/\/+$/, "");
  return withTitle(/\/FeatureServer\/\d+$/.test(url) ? url : firstObjectLayer(url, log), item.title);
}

async function firstObjectLayer(service: string, log: Log): Promise<string> {
  const svc = await fetchJson<{ layers?: { id: number; name: string }[] }>(`${service}?f=json`, log);
  for (const l of svc.layers ?? []) {
    const layer = await fetchJson<Partial<ObjectLayerJson>>(`${service}/${l.id}?f=json`, log);
    if (layer.infoFor3D) {
      log(`layer ${l.id} "${l.name}" is a 3D object layer`);
      return `${service}/${l.id}`;
    }
    log(`layer ${l.id} "${l.name}" has no infoFor3D`);
  }
  throw new Error(`${service} has no 3D object layer`);
}

export async function fetchObjectLayer(url: string, log: Log = () => {}): Promise<ObjectLayerData> {
  const base = url.replace(/\/+$/, "");
  const rest = /\/FeatureServer\/\d+$/.test(base);
  const layer = await fetchJson<ObjectLayerJson>(rest ? `${base}?f=json` : `${base}/layer.json`, log);
  if (!layer.infoFor3D?.transformFieldRoles) {
    throw new Error(`${url} is not a 3D object feature layer (no infoFor3D)`);
  }
  log(`layer "${layer.name}": spatial reference ${JSON.stringify(layer.spatialReference)}, ` +
      `capabilities ${layer.capabilities ?? "(not listed)"}`);
  // query3D needs the Query capability; without it the server only says
  // "This operation is not supported" (e.g. Urban model layers).
  if (rest && layer.capabilities !== undefined &&
      !layer.capabilities.split(",").map((c) => c.trim()).includes("Query")) {
    throw new Error(`${url} does not allow queries (capabilities: ${layer.capabilities}), so its ` +
                    "models and transforms cannot be read; the owner can enable Query or " +
                    "publish a view layer with Query");
  }
  const format = modelFormat(layer.infoFor3D.queryFormats);
  log(`models as ${format} (query formats: ${(layer.infoFor3D.queryFormats ?? []).join(", ") || "not listed"})`);
  // query3D lists the models (assetMaps) only with returnGeometry=true
  // (ArcGIS Online, Zurich layer); the geometry itself is an extent, unused.
  const q = await fetchJson<{ features: ObjectLayerData["features"]; assetMaps?: AssetMapJson[] }>(
    rest ? `${base}/query3D?where=1%3D1&outFields=*&returnGeometry=true` +
           `&formatOf3DObjects=${format}&f=json`
         : `${base}/query3D.json`, log);
  log(`${q.features?.length ?? 0} features, ${q.assetMaps?.length ?? 0} asset maps ` +
      `(${(q.assetMaps ?? []).map((a) => `${a.assetName} ${a.assetType} ` +
        `${a.conversionStatus ?? ""}`).join("; ")})`);
  if (q.features?.length && !q.assetMaps?.length) {
    log(`no asset maps for ${format}: the service has no converted models in that ` +
        "format, or query3D omitted them");
  }
  // Static mocks give asset URLs relative to their directory.
  const absolute = new URL(`${base}/`, location.href);
  const assetMaps = (q.assetMaps ?? []).map((a) => ({
    ...a, assetURL: new URL(a.assetURL, absolute).href,
  }));
  return { url, layer, features: q.features ?? [], assetMaps, format };
}

/**
 * The CRS of a layer's spatial reference: the compound "EPSG:<h>+<v>" with a
 * vertical CRS (heights relative to it, e.g. EGM96), else "EPSG:<h>" promoted
 * to 3D, taking z as ellipsoidal height (an assumption: without vcsWkid the
 * layer does not say).
 */
export function crsDefinition(sr: SpatialReferenceJson): { definition: string; promote3D: boolean } {
  const h = sr.latestWkid ?? sr.wkid;
  const v = sr.latestVcsWkid ?? sr.vcsWkid;
  if (!h) throw new Error("spatial reference without wkid");
  return v ? { definition: `EPSG:${h}+${v}`, promote3D: false }
           : { definition: `EPSG:${h}`, promote3D: true };
}

export interface Wrapper {
  /** The wrapper layer (usda). */
  usda: string;
  /** Models to fetch: URL -> path relative to the wrapper. */
  models: { url: string; path: string; size?: number }[];
  /** Features without a model in the layer's format (skipped). */
  skipped: string[];
  /** Exact duplicates: anchor path -> the first anchor with the same model
   *  (asset hash) and the same transform fields. */
  duplicates: Record<string, string>;
}

/**
 * The wrapper layer for a 3D object layer: one GeospatialCRS from the layer's
 * spatial reference (crsWkt: its WKT2), and per feature an anchor at the
 * feature's origin whose child prim applies the feature's transform and
 * references the model (glb or USD, see MODEL_FORMATS). Y-up and metres like
 * glTF, so the proposal's Y-up axis mapping places the models' x east, y up,
 * z south.
 */
export function buildWrapper(data: ObjectLayerData, crsWkt: string): Wrapper {
  const roles = data.layer.infoFor3D.transformFieldRoles;
  const num = (attrs: Record<string, unknown>, role: string, fallback: number) => {
    const v = attrs[roles[role] ?? ""];
    return typeof v === "number" && Number.isFinite(v) ? v : fallback;
  };
  const models: Wrapper["models"] = [];
  const duplicates: Wrapper["duplicates"] = {};
  const firstOf = new Map<string, string>();
  const skipped: string[] = [];
  const prims: string[] = [];
  for (const f of data.features) {
    const a = f.attributes;
    const globalId = String(a[roles.globalID ?? "GlobalID"] ?? "").replace(/[{}]/g, "").toLowerCase();
    const oid = String(a.OBJECTID ?? a.objectid ?? prims.length + 1);
    const asset = data.assetMaps.find((m) =>
      m.parentGlobalId.replace(/[{}]/g, "").toLowerCase() === globalId &&
      m.assetType === data.format);
    if (!asset) {
      skipped.push(`feature ${oid}: no ${data.format} model`);
      continue;
    }
    const path = `assets/${asset.assetHash}.${EXTENSION[data.format] ?? "glb"}`;
    if (!models.some((m) => m.path === path)) models.push({ url: asset.assetURL, path, size: asset.size });

    const origin = [num(a, "originX", 0), num(a, "originY", 0), num(a, "originZ", 0)];
    // Exact duplicate: the same model file and the same transform fields.
    const key = [asset.assetHash, ...["originX", "originY", "originZ", "translationX", "translationY",
      "translationZ", "scaleX", "scaleY", "scaleZ", "rotationX", "rotationY", "rotationZ", "rotationDeg"]
      .map((r) => a[roles[r] ?? ""] ?? "")].join("|");
    const anchor = `/ObjectLayer/Feature_${oid}`;
    const first = firstOf.get(key);
    if (first) duplicates[anchor] = first; else firstOf.set(key, anchor);
    // The fields are in the model's glTF frame (x east, y up, z south), which
    // is the wrapper's Y-up frame: used as they are (see the header).
    const t = [num(a, "translationX", 0), num(a, "translationY", 0), num(a, "translationZ", 0)];
    const s = [num(a, "scaleX", 1), num(a, "scaleY", 1), num(a, "scaleZ", 1)];
    const axis = [num(a, "rotationX", 0), num(a, "rotationY", 1), num(a, "rotationZ", 0)];
    const q = quaternion(axis, num(a, "rotationDeg", 0));
    prims.push(`
    def Xform "Feature_${oid}" (
        prepend apiSchemas = ["GeospatialCRSBindingAPI"]
        prepend references = </ObjectLayer/CRS>
    )
    {
        double3 crs:position = (${origin.map(fmt).join(", ")})
        custom string arcgis:globalId = "${globalId}"

        def Xform "Model" (
            prepend references = @./${path}@
        )
        {
            double3 xformOp:translate = (${t.map(fmt).join(", ")})
            quatd xformOp:orient = (${q.map(fmt).join(", ")})
            double3 xformOp:scale = (${s.map(fmt).join(", ")})
            uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:orient", "xformOp:scale"]
        }
    }`);
  }
  const usda = `#usda 1.0
(
    defaultPrim = "ObjectLayer"
    doc = """Generated from the ArcGIS 3D object layer ${data.url.replace(/"/g, "%22")}"""
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "ObjectLayer"
{
    def GeospatialCRS "CRS"
    {
        uniform token crs:wkt = "${escape(crsWkt)}"
    }
${prims.join("\n")}
}
`;
  return { usda, models, skipped, duplicates };
}

/** Unit quaternion (w, x, y, z) of a right-handed rotation about `axis`. */
function quaternion(axis: number[], degrees: number): number[] {
  const len = Math.hypot(axis[0], axis[1], axis[2]);
  if (!len || !degrees) return [1, 0, 0, 0];
  const half = (degrees * Math.PI) / 360;
  const k = Math.sin(half) / len;
  return [Math.cos(half), axis[0] * k, axis[1] * k, axis[2] * k];
}

function fmt(v: number): string {
  return Number.isInteger(v) ? v.toFixed(1) : String(v);
}

function escape(s: string): string {
  return s.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, " ");
}

export async function fetchJson<T>(url: string, log: Log = () => {}): Promise<T> {
  log(`GET ${url}`);
  let res: Response;
  try {
    res = await fetch(url);
  } catch (e) {
    throw new Error(`${url}: ${String((e as Error)?.message ?? e)} (network, CORS or COEP)`);
  }
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  const json = await res.json();
  if (json?.error) throw new Error(`${url}: ${JSON.stringify(json.error)}`);
  return json as T;
}
