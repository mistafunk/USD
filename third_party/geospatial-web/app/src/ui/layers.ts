// Layers tab (D27): ArcGIS 3D object layers to load. Search lists an
// organization's public 3D object layers (discover.ts), loadable or not with
// the reason; results are kept in this browser across page loads. The layer
// field (#item, with #itemPresets suggestions) loads anything typed: a
// FeatureServer URL, an item id or an item page. Test layers (the Zurich
// office layer and the local mocks) are in the Development section.
import { findObjectLayers, type Discovery, type FoundLayer } from "../arcgis/discover";
import { combobox } from "./combo";
import { epsgLinks, h } from "./dom";

const STORAGE_KEY = "geospatial-web.discovery.v2";
/** Earlier format: [url, label][] of loadable layers only. */
const OLD_KEY = "geospatial-web.foundLayers";

/** A layer for development and tests, loaded from the Development section. */
export interface TestLayer {
  url: string;
  title: string;
  features: number;
  crs: string;
}

/** Format filter: query3D formats per choice. */
const FORMATS: Record<string, string[]> = {
  gltf: ["3D_glb", "3D_gltf"],
  usd: ["3D_usd", "3D_usdz", "3D_usdc", "3D_usda"],
};

/** The Zurich office layer and the local mocks (testdata/arcgis). */
export const TEST_LAYERS: TestLayer[] = [
  { url: "https://services2.arcgis.com/cFEFS0EWrhfDeVw9/arcgis/rest/services/Zurich_Office_4th_Floor/FeatureServer/0",
    title: "Zurich office", features: 2, crs: "EPSG:4326+5773" },
  { url: "/usd/fixtures/arcgis/zurich-egm96", title: "mock EGM96", features: 2, crs: "EPSG:4326+5773" },
  { url: "/usd/fixtures/arcgis/zurich-ellipsoidal", title: "mock ellipsoidal", features: 2, crs: "EPSG:4326" },
  { url: "/usd/fixtures/arcgis/zurich-usd", title: "mock USD", features: 2, crs: "EPSG:4326" },
  { url: "/usd/fixtures/arcgis/transform", title: "mock transform", features: 1, crs: "EPSG:4326" },
];

export interface LayersOptions {
  /** The layer loaded now (the page's &layer=), if any. */
  current?: string;
  /** Default organization to search. */
  portal: string;
  log(level: "info" | "error", text: string): void;
  /** Loads a layer (in place, see main.ts loadLayer). */
  load(url: string): void;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** "EPSG:4326 + 5773 EGM96" or "EPSG:4326 (z ellipsoidal)". */
function crsBadge(crs: string): HTMLElement {
  if (!crs) return h("span", { class: "badge" }, "CRS not read");
  const v = crs.split("+")[1];
  return v
    ? h("span", { class: "badge info" }, ...epsgLinks(crs, ""), v === "5773" ? " EGM96" : "")
    : h("span", { class: "badge" }, ...epsgLinks(crs, ""), " (z ellipsoidal)");
}

export class LayersTab {
  private discovery: Discovery | null = readStored();
  private text = "";
  private loadableOnly = true;
  private vcs = "any";
  private format = "any";
  private current: string | undefined;

  constructor(private readonly options: LayersOptions) {
    this.current = options.current;
    combobox($<HTMLInputElement>("org"), $<HTMLDataListElement>("orgPresets"));
    combobox($<HTMLInputElement>("item"), $<HTMLDataListElement>("itemPresets"));
    $<HTMLInputElement>("org").value = new URL(this.discovery?.org ?? options.portal).host;
    $<HTMLInputElement>("item").value = options.current ?? "";
    $("itemForm").addEventListener("submit", (event) => {
      event.preventDefault();
      this.load($<HTMLInputElement>("item").value.trim());
    });
    $("find").addEventListener("click", () => void this.search());
    $<HTMLInputElement>("layerFilter").addEventListener("input", (e) => {
      this.text = (e.target as HTMLInputElement).value.toLowerCase();
      this.render();
    });
    $<HTMLInputElement>("loadableOnly").addEventListener("change", (e) => {
      this.loadableOnly = (e.target as HTMLInputElement).checked;
      this.render();
    });
    document.querySelectorAll<HTMLButtonElement>("#formatFilter button").forEach((b) => {
      b.addEventListener("click", () => {
        this.format = b.dataset.format!;
        document.querySelectorAll("#formatFilter button").forEach((x) => x.classList.toggle("active", x === b));
        this.render();
      });
    });
    document.querySelectorAll<HTMLButtonElement>("#vcsFilter button").forEach((b) => {
      b.addEventListener("click", () => {
        this.vcs = b.dataset.v!;
        document.querySelectorAll("#vcsFilter button").forEach((x) => x.classList.toggle("active", x === b));
        this.render();
      });
    });
    this.render();
  }

  load(url: string) {
    this.options.load(url);
  }

  /** The layer loaded now: marked "loaded" and shown in the layer field. */
  setCurrent(url: string | undefined) {
    this.current = url;
    $<HTMLInputElement>("item").value = url ?? "";
    this.render();
  }

  private async search() {
    const typed = $<HTMLInputElement>("org").value.trim();
    let org = typed ? (/^https?:\/\//.test(typed) ? typed : `https://${typed}`) : this.options.portal;
    // ArcGIS Online itself: its REST API is on www.arcgis.com.
    org = org.replace(/^https:\/\/arcgis\.com\b/, "https://www.arcgis.com");
    const button = $<HTMLButtonElement>("find");
    button.disabled = true;
    this.options.log("info", `searching 3D object layers on ${org}`);
    try {
      const d = await findObjectLayers(org, (t) => this.options.log("info", t),
        (done, total) => { button.textContent = `${done}/${total}`; });
      this.discovery = d;
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(d));
      } catch {
        // Not kept across page loads, still listed now.
      }
      const ok = d.layers.filter((l) => l.loadable).length;
      this.options.log("info", `found ${ok} loadable, ${d.layers.length - ok} rejected layers on ${org}`);
      this.render();
    } catch (e) {
      this.options.log("error", `search on ${org}: ${String((e as Error)?.message ?? e)}`);
    } finally {
      button.disabled = false;
      button.textContent = "Search";
    }
  }

  private matches(l: { title: string; owner?: string; crs: string; loadable?: boolean;
                       formats?: string[] }): boolean {
    if (this.format !== "any" && !(l.formats ?? []).some((f) => FORMATS[this.format].includes(f))) {
      return false;
    }
    if (this.loadableOnly && l.loadable === false) return false;
    if (this.text && !`${l.title} ${l.owner ?? ""}`.toLowerCase().includes(this.text)) return false;
    const v = l.crs.split("+")[1];
    if (this.vcs === "none" && v) return false;
    if (this.vcs !== "any" && this.vcs !== "none" && v !== this.vcs) return false;
    return true;
  }

  private render() {
    const d = this.discovery;
    const found = d?.layers ?? [];
    const loadable = found.filter((l) => l.loadable).length;
    const shown = found.filter((l) => this.matches(l));
    $("layerCards").replaceChildren(...[
      h("div", { class: "section-title" }, !d ? "Organization layers"
        : `${d.scope === "public" ? "ArcGIS Online" : "Organization"} layers · ${loadable} loadable, ` +
          `${found.length - loadable} rejected` +
          (d.total && d.total > found.length ? ` (most viewed of ${d.total})` : "")),
      ...shown.map((l) => this.card(l)),
      h("div", { class: "empty muted small" }, !d
        ? "Search lists the public 3D object layers of an organization, or of all of ArcGIS Online (arcgis.com)."
        : !shown.length ? "none matches the filter" : ""),
    ].filter((e) => e.textContent));
    // Suggestions for the layer field: loadable layers, then the test layers.
    $("itemPresets").replaceChildren(...[...found.filter((l) => l.loadable), ...TEST_LAYERS].map((l) =>
      h("option", { value: l.url, label: `${l.title} (${l.features} features, ${l.crs})` })));
  }

  private card(l: FoundLayer): HTMLElement {
    const loaded = !!this.current && this.current === l.url;
    const thumb = l.thumbnail
      ? h("img", { class: "thumb", src: l.thumbnail, alt: "", crossorigin: "anonymous", loading: "lazy" })
      : h("div", { class: "thumb" }, "⬢");
    thumb.addEventListener("error", () => thumb.replaceWith(h("div", { class: "thumb" }, "⬢")));
    const meta = [l.owner, l.modified ? `modified ${new Date(l.modified).toISOString().slice(0, 10)}` : ""]
      .filter(Boolean).join(" · ");
    return h("div", { class: `card layer${loaded ? " loaded" : ""}${l.loadable ? "" : " rejected"}`,
                      "data-url": l.url },
      thumb,
      h("div", { style: "min-width: 0" },
        h("div", { class: "ttl", title: l.url }, l.title),
        l.loadable
          ? h("div", { class: "sub2" }, h("span", null, `${l.features} feature${l.features === 1 ? "" : "s"}`),
              crsBadge(l.crs))
          : h("div", { class: "sub2 reason" }, l.reason ?? "cannot be loaded"),
        meta ? h("div", { class: "sub2" }, h("span", null, meta)) : null,
        l.itemId && this.discovery ? h("div", { class: "sub2" }, h("a", {
          href: `${this.discovery!.org.replace(/\/+$/, "")}/home/item.html?id=${l.itemId}`,
          target: "_blank", rel: "noopener",
        }, "item page")) : null),
      loaded ? h("span", { class: "badge ok" }, "loaded")
        : l.loadable ? h("button", { class: "btn sm", type: "button", onclick: () => this.load(l.url) }, "Load")
        : h("span", null));
  }
}

function readStored(): Discovery | null {
  try {
    const v = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "null");
    if (v && Array.isArray(v.layers)) return v as Discovery;
    // Earlier format: loadable layers as [url, "title (n features, crs)"].
    const old = JSON.parse(localStorage.getItem(OLD_KEY) ?? "[]");
    if (!Array.isArray(old) || !old.length) return null;
    return {
      org: "https://zurich.maps.arcgis.com", orgName: "earlier search", searchedAt: Date.now(),
      layers: old.filter((e) => Array.isArray(e) && e.length === 2).map(([url, label]: [string, string]) => {
        const m = /^(.*) \((\d+) features, (EPSG:[0-9+]+)\)$/.exec(label);
        return { url, title: m?.[1] ?? label, itemId: "", loadable: true,
                 features: Number(m?.[2] ?? 0), crs: m?.[3] ?? "" };
      }),
    };
  } catch {
    return null;
  }
}
