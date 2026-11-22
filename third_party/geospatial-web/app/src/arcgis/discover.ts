// Finds the public 3D object feature layers of an ArcGIS organization that
// the app can load: items of type Feature Service with the 3DObject type
// keyword, whose layers have infoFor3D, allow Query and return a model in
// their preferred format from query3D (USD when offered, else glb:
// objectLayer.ts modelFormat). Anonymous, so only public items.
import { fetchJson, modelFormat, type Log, type ObjectLayerJson } from "./objectLayer";

export interface FoundLayer {
  url: string;
  title: string;
  features: number;
  /** "EPSG:<h>" or "EPSG:<h>+<v>". */
  crs: string;
}

interface SearchResult {
  total: number;
  nextStart: number;
  results: { id: string; title: string; url?: string }[];
}

/**
 * The organization's loadable 3D object layers. `portal` is the
 * organization's address (e.g. https://zurich.maps.arcgis.com);
 * `progress(done, total)` follows the service checks.
 */
export async function findObjectLayers(
  portal: string, log: Log, progress: (done: number, total: number) => void,
): Promise<FoundLayer[]> {
  const rest = `${portal.replace(/\/+$/, "")}/sharing/rest`;
  const self = await fetchJson<{ id?: string; name?: string }>(`${rest}/portals/self?f=json`, log);
  if (!self.id) throw new Error(`${portal}: not an ArcGIS organization (no id in portals/self)`);
  const q = `orgid:${self.id} AND type:"Feature Service" AND typekeywords:"3DObject"`;
  const items: SearchResult["results"] = [];
  for (let start = 1; start > 0;) {
    const page = await fetchJson<SearchResult>(
      `${rest}/search?q=${encodeURIComponent(q)}&num=100&start=${start}&sortField=title&f=json`, log);
    items.push(...page.results);
    start = page.nextStart;
  }
  log(`${self.name ?? self.id}: ${items.length} public 3D object feature services`);

  let done = 0;
  progress(done, items.length);
  const found: FoundLayer[] = [];
  // A few services at a time.
  const queue = [...items];
  const worker = async () => {
    for (let it = queue.shift(); it; it = queue.shift()) {
      try {
        found.push(...await checkService(it.title, (it.url ?? "").replace(/\/+$/, ""), log));
      } catch (e) {
        log(`${it.title}: ${String((e as Error)?.message ?? e)}`);
      }
      progress(++done, items.length);
    }
  };
  await Promise.all(Array.from({ length: 4 }, worker));
  return found.sort((a, b) => a.title.localeCompare(b.title));
}

async function checkService(title: string, service: string, log: Log): Promise<FoundLayer[]> {
  if (!service) throw new Error("no service URL");
  const quiet: Log = () => {};
  const svc = await fetchJson<{ layers?: { id: number }[] }>(`${service}?f=json`, quiet);
  const found: FoundLayer[] = [];
  for (const l of svc.layers ?? []) {
    const url = `${service}/${l.id}`;
    const layer = await fetchJson<ObjectLayerJson>(`${url}?f=json`, quiet);
    if (!layer.infoFor3D) continue;
    const caps = (layer.capabilities ?? "").split(",").map((c) => c.trim());
    if (!caps.includes("Query")) {
      log(`${title} (layer ${l.id}): Query off (${layer.capabilities})`);
      continue;
    }
    const q = await fetchJson<{ assetMaps?: { assetType: string }[] }>(
      `${url}/query3D?where=1%3D1&outFields=*&returnGeometry=true&resultRecordCount=1` +
      `&formatOf3DObjects=${modelFormat(layer.infoFor3D.queryFormats)}&f=json`, quiet);
    const format = modelFormat(layer.infoFor3D.queryFormats);
    if (!(q.assetMaps ?? []).some((a) => a.assetType === format)) {
      log(`${title} (layer ${l.id}): no ${format} model`);
      continue;
    }
    const n = await fetchJson<{ count?: number }>(`${url}/query?where=1%3D1&returnCountOnly=true&f=json`, quiet);
    const sr = layer.spatialReference;
    const h = sr.latestWkid ?? sr.wkid;
    const v = sr.latestVcsWkid ?? sr.vcsWkid;
    const crs = `EPSG:${h}${v ? `+${v}` : ""}`;
    log(`${title} (layer ${l.id}): ${n.count ?? "?"} features, ${crs}`);
    found.push({ url, title, features: n.count ?? 0, crs });
  }
  return found;
}
