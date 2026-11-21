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
// Transform semantics (from the SDK's mesh origin and MeshTransform; the
// order and the frame are not documented and still to be verified on a
// rotated feature): a model point p (metres east/north/up at the origin) is
// placed at R(axis, angle) * (scale * p) + translation, the rotation
// right-handed about an east/north/up axis.

export interface SpatialReferenceJson {
  wkid?: number;
  latestWkid?: number;
  vcsWkid?: number;
  latestVcsWkid?: number;
}

export interface ObjectLayerJson {
  name?: string;
  spatialReference: SpatialReferenceJson;
  infoFor3D: { transformFieldRoles: Record<string, string> };
}

export interface AssetMapJson {
  parentGlobalId: string;
  assetName: string;
  assetHash: string;
  assetType: string;
  assetURL: string;
  conversionStatus?: string;
}

export interface ObjectLayerData {
  url: string;
  layer: ObjectLayerJson;
  features: { attributes: Record<string, unknown> }[];
  assetMaps: AssetMapJson[];
}

/** The 3D format the wrapper references (glTF binary). */
export const MODEL_FORMAT = "3D_glb";

export async function fetchObjectLayer(url: string): Promise<ObjectLayerData> {
  const base = url.replace(/\/+$/, "");
  const rest = /\/FeatureServer\/\d+$/.test(base);
  const layer = await fetchJson<ObjectLayerJson>(rest ? `${base}?f=json` : `${base}/layer.json`);
  if (!layer.infoFor3D?.transformFieldRoles) {
    throw new Error(`${url} is not a 3D object feature layer (no infoFor3D)`);
  }
  const q = await fetchJson<{ features: ObjectLayerData["features"]; assetMaps?: AssetMapJson[] }>(
    rest ? `${base}/query3D?where=1%3D1&outFields=*&returnGeometry=false` +
           `&formatOf3DObjects=${MODEL_FORMAT}&f=json`
         : `${base}/query3D.json`);
  // Static mocks give asset URLs relative to their directory.
  const absolute = new URL(`${base}/`, location.href);
  const assetMaps = (q.assetMaps ?? []).map((a) => ({
    ...a, assetURL: new URL(a.assetURL, absolute).href,
  }));
  return { url, layer, features: q.features ?? [], assetMaps };
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
  models: { url: string; path: string }[];
  /** Features without a model in MODEL_FORMAT (skipped). */
  skipped: string[];
}

/**
 * The wrapper layer for a 3D object layer: one GeospatialCRS from the layer's
 * spatial reference (crsWkt: its WKT2), and per feature an anchor at the
 * feature's origin whose child prim applies the feature's transform and
 * references the model. Y-up and metres like glTF, so the proposal's Y-up
 * axis mapping places the models' x east, y up, z south.
 */
export function buildWrapper(data: ObjectLayerData, crsWkt: string): Wrapper {
  const roles = data.layer.infoFor3D.transformFieldRoles;
  const num = (attrs: Record<string, unknown>, role: string, fallback: number) => {
    const v = attrs[roles[role] ?? ""];
    return typeof v === "number" && Number.isFinite(v) ? v : fallback;
  };
  const models: Wrapper["models"] = [];
  const skipped: string[] = [];
  const prims: string[] = [];
  for (const f of data.features) {
    const a = f.attributes;
    const globalId = String(a[roles.globalID ?? "GlobalID"] ?? "").replace(/[{}]/g, "").toLowerCase();
    const oid = String(a.OBJECTID ?? a.objectid ?? prims.length + 1);
    const asset = data.assetMaps.find((m) =>
      m.parentGlobalId.replace(/[{}]/g, "").toLowerCase() === globalId &&
      m.assetType === MODEL_FORMAT);
    if (!asset) {
      skipped.push(`feature ${oid}: no ${MODEL_FORMAT} model`);
      continue;
    }
    const path = `assets/${asset.assetHash}.glb`;
    if (!models.some((m) => m.path === path)) models.push({ url: asset.assetURL, path });

    const origin = [num(a, "originX", 0), num(a, "originY", 0), num(a, "originZ", 0)];
    // East/north/up -> the wrapper's Y-up axes: (e, n, u) -> (e, u, -n).
    const enuToStage = (v: number[]) => [v[0], v[2], -v[1]];
    const t = enuToStage([num(a, "translationX", 0), num(a, "translationY", 0),
                          num(a, "translationZ", 0)]);
    const s = [num(a, "scaleX", 1), num(a, "scaleZ", 1), num(a, "scaleY", 1)];
    const axis = enuToStage([num(a, "rotationX", 0), num(a, "rotationY", 0),
                             num(a, "rotationZ", 1)]);
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
  return { usda, models, skipped };
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

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  const json = await res.json();
  if (json?.error) throw new Error(`${url}: ${JSON.stringify(json.error)}`);
  return json as T;
}
