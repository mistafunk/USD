// Finds public 3D object feature layers: items of type Feature Service with
// the 3DObject type keyword, of an ArcGIS organization or, for ArcGIS Online
// itself (arcgis.com, which has no organization id), of all organizations
// (most viewed first, up to MAX_PUBLIC). A layer is loadable
// when it has infoFor3D, allows Query and query3D returns a model in its
// preferred format (USD when offered, else glb: objectLayer.ts modelFormat);
// the others are listed with the reason. Anonymous, so only public items.
import { fetchJson, modelFormat, type Log, type ObjectLayerJson } from "./objectLayer";

export interface FoundLayer {
  url: string;
  title: string;
  /** The portal item and its metadata from the search. */
  itemId: string;
  owner?: string;
  /** Milliseconds since 1970. */
  modified?: number;
  /** Thumbnail image URL, if the item has one. */
  thumbnail?: string;
  /** False when the app cannot load it; `reason` says why. */
  loadable: boolean;
  reason?: string;
  features: number;
  /** "EPSG:<h>" or "EPSG:<h>+<v>"; empty when not read. */
  crs: string;
  /** Formats query3D can return (infoFor3D.queryFormats), e.g. "3D_glb";
   *  undefined when not read (older stored searches). */
  formats?: string[];
}

/** Items checked at most when searching all of ArcGIS Online. */
export const MAX_PUBLIC = 200;

/** A Find run: what was searched and what was found (D27). */
export interface Discovery {
  org: string;
  orgName: string;
  /** "org": one organization's items; "public": all public items of ArcGIS Online. */
  scope?: "org" | "public";
  /** Items the search matched (more than checked when capped). */
  total?: number;
  /** Milliseconds since 1970. */
  searchedAt: number;
  layers: FoundLayer[];
}

interface SearchItem {
  id: string;
  title: string;
  url?: string;
  owner?: string;
  modified?: number;
  thumbnail?: string | null;
}

interface SearchResult {
  total: number;
  nextStart: number;
  results: SearchItem[];
}

/**
 * The organization's 3D object layers, loadable or not (with the reason).
 * `portal` is the organization's address (e.g. https://zurich.maps.arcgis.com);
 * `progress(done, total)` follows the service checks.
 */
export async function findObjectLayers(
  portal: string, log: Log, progress: (done: number, total: number) => void,
): Promise<Discovery> {
  const rest = `${portal.replace(/\/+$/, "")}/sharing/rest`;
  const self = await fetchJson<{ id?: string | null; name?: string | null }>(`${rest}/portals/self?f=json`, log);
  // ArcGIS Online itself (www.arcgis.com) has no organization id: search all
  // public items, most viewed first.
  const scope = self.id ? "org" : "public";
  const orgName = self.id ? self.name ?? self.id : "ArcGIS Online (all organizations)";
  const q = `${self.id ? `orgid:${self.id} AND ` : ""}type:"Feature Service" AND typekeywords:"3DObject"`;
  const sort = scope === "org" ? "sortField=title" : "sortField=numviews&sortOrder=desc";
  const items: SearchResult["results"] = [];
  let total = 0;
  for (let start = 1; start > 0 && (scope === "org" || items.length < MAX_PUBLIC);) {
    const page = await fetchJson<SearchResult>(
      `${rest}/search?q=${encodeURIComponent(q)}&num=100&start=${start}&${sort}&f=json`, log);
    items.push(...page.results);
    total = page.total;
    start = page.nextStart;
  }
  if (scope === "public") items.splice(MAX_PUBLIC);
  log(`${orgName}: ${total} public 3D object feature services` +
      (items.length < total ? `, checking the ${items.length} most viewed` : ""));

  let done = 0;
  progress(done, items.length);
  const found: FoundLayer[] = [];
  // A few services at a time.
  const queue = [...items];
  const worker = async () => {
    for (let it = queue.shift(); it; it = queue.shift()) {
      const item: Omit<FoundLayer, "url" | "loadable" | "features" | "crs"> = {
        title: it.title, itemId: it.id, owner: it.owner, modified: it.modified,
        thumbnail: it.thumbnail ? `${rest}/content/items/${it.id}/info/${it.thumbnail}` : undefined,
      };
      try {
        found.push(...await checkService(item, (it.url ?? "").replace(/\/+$/, ""), log));
      } catch (e) {
        const reason = String((e as Error)?.message ?? e);
        log(`${it.title}: ${reason}`);
        found.push({ ...item, url: it.url ?? "", loadable: false, reason, features: 0, crs: "" });
      }
      progress(++done, items.length);
    }
  };
  await Promise.all(Array.from({ length: 4 }, worker));
  // An organization alphabetically; ArcGIS Online in search order (most viewed).
  const order = new Map(items.map((it, i) => [it.id, i]));
  found.sort(scope === "org" ? (a, b) => a.title.localeCompare(b.title)
                             : (a, b) => (order.get(a.itemId) ?? 0) - (order.get(b.itemId) ?? 0));
  return { org: portal, orgName, scope, total, searchedAt: Date.now(), layers: found };
}

async function checkService(item: Omit<FoundLayer, "url" | "loadable" | "features" | "crs">,
                            service: string, log: Log): Promise<FoundLayer[]> {
  const title = item.title;
  if (!service) throw new Error("no service URL");
  const quiet: Log = () => {};
  const svc = await fetchJson<{ layers?: { id: number }[] }>(`${service}?f=json`, quiet);
  const found: FoundLayer[] = [];
  for (const l of svc.layers ?? []) {
    const url = `${service}/${l.id}`;
    const layer = await fetchJson<ObjectLayerJson>(`${url}?f=json`, quiet);
    if (!layer.infoFor3D) continue;
    const caps = (layer.capabilities ?? "").split(",").map((c) => c.trim());
    const sr = layer.spatialReference;
    const h = sr.latestWkid ?? sr.wkid;
    const v = sr.latestVcsWkid ?? sr.vcsWkid;
    const crs = `EPSG:${h}${v ? `+${v}` : ""}`;
    const formats = layer.infoFor3D.queryFormats ?? [];
    const rejected = (reason: string) => {
      log(`${title} (layer ${l.id}): ${reason}`);
      found.push({ ...item, url, loadable: false, reason, features: 0, crs, formats });
    };
    if (!caps.includes("Query")) {
      rejected(`Query off (${layer.capabilities})`);
      continue;
    }
    const q = await fetchJson<{ assetMaps?: { assetType: string }[] }>(
      `${url}/query3D?where=1%3D1&outFields=*&returnGeometry=true&resultRecordCount=1` +
      `&formatOf3DObjects=${modelFormat(formats)}&f=json`, quiet);
    if (!(q.assetMaps ?? []).some((a) => a.assetType === modelFormat(formats))) {
      rejected(`no ${modelFormat(formats)} model`);
      continue;
    }
    const n = await fetchJson<{ count?: number }>(`${url}/query?where=1%3D1&returnCountOnly=true&f=json`, quiet);
    log(`${title} (layer ${l.id}): ${n.count ?? "?"} features, ${crs}`);
    found.push({ ...item, url, loadable: true, features: n.count ?? 0, crs, formats });
  }
  return found;
}
