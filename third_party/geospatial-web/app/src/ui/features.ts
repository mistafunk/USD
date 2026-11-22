// Features tab (D27): the output CRS and, per anchor, its source CRS,
// placement, position, display offset, meshes and test case. Live data adds
// the module's CRS descriptions and fetched grids; recorded
// data shows what its manifest has.
import type { HarnessReport } from "../harness";
import { GRAVITY_VCS, type LiveData } from "../live";
import type { AnchorPlacement } from "../render";
import type { Scene } from "../scene";
import type { CrsDescription } from "../usd/types";
import { copyablePre, epsgLinks, h, kv } from "./dom";

export interface FeaturesData {
  global: boolean;
  scene: Scene | null;
  live: LiveData | null;
  report: HarnessReport | null;
}

export interface FeaturesHooks {
  /** An anchor row was selected (null: none). */
  select(path: string | null): void;
  /** Draw only this anchor's meshes (null: all). */
  solo(path: string | null): void;
  /** Do not draw these anchors' meshes. */
  mute(paths: Set<string>): void;
  zoom(path: string): void;
}

const STATUS_ORDER: Record<string, number> = { error: 0, unsupported: 1, ok: 2 };

const $ = (id: string) => document.getElementById(id)!;

export class FeaturesTab {
  private selected: string | null = null;
  private soloed: string | null = null;
  private readonly muted = new Set<string>();
  private readonly open = new Set<string>();
  private data: FeaturesData | null = null;

  constructor(private readonly hooks: FeaturesHooks) {}

  render(data: FeaturesData) {
    this.data = data;
    this.renderHeader(data);
    this.renderInput(data);
    this.renderOutput(data);
    this.renderAnchors(data);
  }

  /** Selects an anchor (row and highlight), e.g. from a test. */
  /** Draws only this anchor (null: all anchors), e.g. from a test. */
  solo(path: string | null) {
    this.soloed = path;
    const list = document.getElementById("anchorList")!;
    list.classList.toggle("soloing", path !== null);
    list.querySelectorAll<HTMLElement>(".anchor").forEach((a) => {
      const on = a.dataset.path === path;
      a.classList.toggle("soloed", on);
      a.querySelector(".solo")?.classList.toggle("active", on);
    });
    this.hooks.solo(path);
  }

  /** Mutes or unmutes an anchor (its meshes are not drawn); null: unmutes all. */
  mute(path: string | null, on = true) {
    if (path === null) this.muted.clear();
    else if (on) this.muted.add(path);
    else this.muted.delete(path);
    document.querySelectorAll<HTMLElement>("#anchorList .anchor").forEach((a) => {
      const m = this.muted.has(a.dataset.path!);
      a.classList.toggle("muted", m);
      a.querySelector(".mute")?.classList.toggle("active", m);
    });
    this.hooks.mute(new Set(this.muted));
  }

  select(path: string | null) {
    this.selected = path;
    document.querySelectorAll<HTMLElement>("#anchorList .anchor")
      .forEach((a) => a.classList.toggle("selected", a.dataset.path === path));
    this.hooks.select(path);
  }

  /** The loaded content's name on top: the 3D object layer, else the test scene or the spike. */
  private renderHeader({ scene, live }: FeaturesData) {
    const layer = live?.objectLayer;
    const title = $("loadedTitle");
    // Feature count and layer link in parentheses after the name; the CRS is
    // in the Input CRS section.
    const extra = (...parts: (Node | string)[]) =>
      h("span", { class: "loaded-extra" }, " (", ...parts, ")");
    if (layer) {
      const n = `${layer.features} feature${layer.features === 1 ? "" : "s"}`;
      title.title = layer.url;
      title.replaceChildren(layer.title, /^https?:/.test(layer.url)
        ? extra(`${n}, `, h("a", { href: layer.url, target: "_blank", rel: "noopener" }, "layer"))
        : extra(n));
    } else if (!scene) {
      title.replaceChildren("loading…");
    } else if (scene.details) {
      const n = scene.details.anchors.length;
      title.title = scene.name;
      title.replaceChildren(live ? "Test scene" : "Test scene, recorded", extra(`${n} feature${n === 1 ? "" : "s"}`));
    } else {
      title.replaceChildren("Spike meshes", extra("hard-coded"));
    }
  }

  /**
   * The layer CRS: the source CRS of the loaded content, like the output CRS
   * below it. A 3D object layer has one; the test scene's features several,
   * each listed with how many features use it.
   */
  private renderInput({ scene, live }: FeaturesData) {
    const body = $("inBody");
    const summary = $("inSummary");
    const layer = live?.objectLayer;
    const groups = new Map<string, { crs: CrsDescription; n: number }>();
    for (const c of Object.values(live?.crs.anchors ?? {})) {
      if (c.error) continue;
      const key = c.wkt ?? c.name ?? "";
      const g = groups.get(key);
      if (g) g.n++; else groups.set(key, { crs: c, n: 1 });
    }
    const all = [...groups.values()];
    if (!live) {
      // Recorded data: the source CRS names of the manifest.
      const names = new Map<string, number>();
      for (const a of scene?.details?.anchors ?? []) {
        if (a.sourceCrs) names.set(a.sourceCrs, (names.get(a.sourceCrs) ?? 0) + 1);
      }
      summary.textContent = names.size === 1 ? [...names.keys()][0] : names.size ? `${names.size} CRSs` : "";
      $("inWhy").replaceChildren();
      body.replaceChildren(kv([...names].map(([n, k]): [string, string] => [`${k} feature${k === 1 ? "" : "s"}`, n])));
      return;
    }
    if (all.length === 1) {
      const c = all[0].crs;
      summary.replaceChildren(...nameWithCode(c.name, crsCode(c)));
      summary.title = summary.textContent ?? "";
      const why = layer
        ? `From the layer's spatial reference (wkid ${layer.spatialReference.latestWkid ?? layer.spatialReference.wkid}` +
          `${layer.spatialReference.vcsWkid ? `, vcsWkid ${layer.spatialReference.latestVcsWkid ?? layer.spatialReference.vcsWkid}` : ""}).` +
          (layer.assumed ? ` It names no vertical CRS, so its z is in ${layer.assumed}, as the Maps SDK draws it.` : "")
        : "From the features' binding: the composed crs:wkt, resolved in OpenUSD.";
      $("inWhy").replaceChildren(...whyHelp(why));
      body.replaceChildren(crsBlock(c, { assumed: layer?.assumed, ids: "in" }));
    } else {
      summary.textContent = all.length ? `${all.length} CRSs` : "";
      $("inWhy").replaceChildren();
      body.replaceChildren(...all.map(({ crs: c, n }) => h("div", { class: "sub" },
        h("h4", null, ...nameWithCode(c.name, crsCode(c)), " ", h("span", { class: "muted small", style: "font-weight: 400" },
                                           `${n} feature${n === 1 ? "" : "s"}`)),
        crsBlock(c))));
    }
  }

  private renderOutput({ global, scene, live }: FeaturesData) {
    const details = scene?.details;
    const crs = live?.output.crs ?? details?.output.crs ?? "";
    const frame = live?.output.frame ?? details?.output.frame;
    // Heights follow the scene's height model (D28).
    const gravity = crs.endsWith(`+${GRAVITY_VCS.slice(5)}`);
    const why = (global
      ? "The global view's render space is ECEF on a 6378137 m sphere, not EPSG:4978. Anchors are " +
        "placed in geographic 3D WGS 84 with an east-north-up frame in metres, and the app maps them " +
        "through the SDK's own geographic-to-render function."
      : "A local view renders in its own spatial reference, so anchors are placed directly in it, " +
        "with 3D heights. Web Mercator is not metric: placement includes its scale, about " +
        "1/cos(latitude).") + (gravity
      ? " Heights are EGM96 (gravity-related), like the terrain's, which the view draws as they are."
      : " Heights are ellipsoidal: without terrain the view's ground is the ellipsoid.");
    // Recorded data has no description: the code alone.
    const c: CrsDescription = { ...(live?.crs.output ?? { error: "" }),
                                wkt: live?.output.wkt || live?.crs.output.wkt };
    if (!c.id) c.id = crs;
    $("outSummary").replaceChildren(...nameWithCode(c.name, crsCode(c)));
    $("outSummary").title = $("outSummary").textContent ?? "";
    $("outWhy").replaceChildren(...whyHelp(why + (frame
      ? ` Each anchor's frame: ${frame === "enu" ? "east-north-up, in metres" : "the CRS's axes and units"}.` : "")));
    // Where the output CRS is defined: today by the app (live, from the view
    // and the height model) or the recording, always as an EPSG code; a
    // definition in USD would come as WKT, which is then shown.
    const horizontal = crs.split("+")[0];
    const definedBy = live
      ? `the app, from the ${global ? "global view (WGS 84)" : `local view's spatial reference (${horizontal})`}` +
        ` and the scene's height model (${gravity ? "terrain: EGM96 heights" : "no terrain: ellipsoidal"})`
      : details ? `the recording (geospatialRecord), for the ${global ? "global" : "local"} view` : undefined;
    $("outBody").replaceChildren(crsBlock(c, {
      ids: "out", heights: gravity ? undefined : "ellipsoidal", definedBy,
      wkt: !!crs && !/^EPSG:\d+(\+\d+)?$/.test(crs),
    }));
  }

  private renderAnchors({ global, scene, live, report }: FeaturesData) {
    const list = $("anchorList");
    const details = scene?.details;
    const anchors = (live?.anchors as AnchorPlacement[] | undefined) ?? details?.anchors ?? [];
    if (!anchors.length) {
      list.replaceChildren(h("div", { class: "empty muted small" },
        scene ? "No anchors: this content has no placement data." : "loading…"));
      return;
    }
    const sorted = anchors
      .map((a, i) => ({ a, i }))
      .sort((x, y) => (STATUS_ORDER[x.a.status] - STATUS_ORDER[y.a.status]) || x.i - y.i)
      .map((x) => x.a);
    // A 3D object layer has one CRS, so its anchors place or fail together;
    // the test scene mixes outcomes, failures first.
    list.replaceChildren(...sorted.map((a) => this.anchorRow(a, global, scene, live, report)));
  }

  private anchorRow(a: AnchorPlacement, global: boolean, scene: Scene | null,
                    live: LiveData | null, report: HarnessReport | null): HTMLElement {
    const details = scene?.details;
    // A suspicious transform (e.g. an absolute translation): "!" in the row,
    // the text in the card.
    const warning = live?.objectLayer?.suspicious[a.path];
    const meshes = live
      ? live.meshes.filter((m) => m.anchor === a.path && m.status === "ok")
      : [...(details?.meshAnchor ?? new Map())].filter(([, anchor]) => anchor === a.path)
        .map(([path]) => ({ path, triangles: undefined, linearizationError: null }));
    const placed = a.status === "ok";
    const zoom = h("button", {
      class: "btn sm zoom", type: "button", title: placed ? "Zoom to anchor" : "Not placed",
      disabled: !placed || !meshes.length,
      onclick: (e: Event) => {
        e.preventDefault();
        e.stopPropagation();
        this.select(a.path);
        this.hooks.zoom(a.path);
      },
    }, "⌖");
    const solo = h("button", {
      class: `btn sm solo${this.soloed === a.path ? " active" : ""}`, type: "button",
      title: placed ? "Solo: show only this feature (click again: all)" : "Not placed",
      disabled: !placed || !meshes.length,
      onclick: (e: Event) => {
        e.preventDefault();
        e.stopPropagation();
        this.solo(this.soloed === a.path ? null : a.path);
      },
    }, "S");
    const mute = h("button", {
      class: `btn sm mute${this.muted.has(a.path) ? " active" : ""}`, type: "button",
      title: placed ? "Mute: hide this feature (click again: show)" : "Not placed",
      disabled: !placed || !meshes.length,
      onclick: (e: Event) => {
        e.preventDefault();
        e.stopPropagation();
        this.mute(a.path, !this.muted.has(a.path));
      },
    }, "M");
    // The arrow expands and collapses the row; the rest of the row selects it.
    const open = this.open.has(a.path);
    const expander = h("button", {
      class: "expander", type: "button", title: "Show details",
      "aria-expanded": String(open), "aria-label": "Show details",
    }, h("span", { class: "tri" }));
    const summary = h("summary", null,
      expander,
      // One line: the path, then mesh count (and duplicate) in parentheses, a
      // "!" for a warning (its text in the card);
      // the source CRS is in the Input CRS section and the details.
      h("span", { class: "path", title: a.path }, a.path,
        h("span", { class: "meshcount" }, ` (${meshes.length} mesh${meshes.length === 1 ? "" : "es"}`,
          live?.objectLayer?.duplicates[a.path]
            ? [", ", h("span", { class: "dup", title: "Same model and transform: drawn at the same place" },
                       `duplicate of ${live.objectLayer.duplicates[a.path].split("/").pop()}`)]
            : null, ")"),
        warning ? h("span", { class: "warnicon", title: warning }, "!") : null),
      h("span", { class: "actions" }, solo, mute, zoom),
      // Two lines here (CSS); the full reason is in the Placement section.
      !placed && a.reason ? h("span", { class: "reason", title: a.reason }, a.reason) : null);
    const row = h("details", {
      class: `anchor s-${a.status}${this.selected === a.path ? " selected" : ""}` +
             `${this.soloed === a.path ? " soloed" : ""}${this.muted.has(a.path) ? " muted" : ""}`,
      "data-path": a.path, "data-status": a.status, open: this.open.has(a.path),
    }, summary, h("div", { class: "body" },
      warning ? h("div", { class: "warnbox" }, warning) : null,
      placed && a.origin ? positionSection(a.origin, global, live?.output.crs ?? details?.output.crs) : null,
      placementSection(a, live),
      meshesSection(meshes),
      testSection(a.path, details?.testCases ?? live?.testCases ?? [], report, !!live)));
    row.addEventListener("toggle", () => {
      if (row.open) this.open.add(a.path); else this.open.delete(a.path);
      expander.setAttribute("aria-expanded", String(row.open));
      expander.title = row.open ? "Hide details" : "Show details";
    });
    expander.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      row.open = !row.open;
    });
    // A click elsewhere in the row selects or deselects; it does not expand.
    summary.addEventListener("click", (e) => {
      e.preventDefault();
      this.select(this.selected === a.path ? null : a.path);
    });
    return row;
  }
}

function accuracyText(accuracy: number | null | undefined, prefix = ""): string {
  return accuracy === null || accuracy === undefined ? "unknown" : `${prefix}${accuracy} m`;
}

/**
 * A CRS as everywhere in the CRS cards: "<name> (<id>)", in one font, the id
 * linked to epsg.io; a promoted CRS as "<name> (<id>, 3D)".
 */
function nameWithCode(name: string | undefined, code: string): (Node | string)[] {
  const id = code.replace(" (3D)", "");
  const links = id ? [...epsgLinks(id, ""), code.endsWith(" (3D)") ? ", 3D" : ""] : [];
  if (!name) return links.length ? links : ["none"];
  return links.length ? [name, " (", ...links, ")"] : [name];
}

function part(p: { name: string; id: string } | undefined) {
  return p ? nameWithCode(p.name, p.id) : undefined;
}

/** A field label with a "?" popover. */
function labelHelp(label: string, help: string): (Node | string)[] {
  return [label, " ", h("button", { class: "help", type: "button", title: `About ${label}` }, "?"),
          h("span", { class: "pop" }, help)];
}

function sub(title: string, badges: (HTMLElement | null)[], ...body: (Node | null)[]): HTMLElement {
  return h("div", { class: "sub" }, h("h4", null, title, ...badges), ...body);
}

function badge(text: string, cls = ""): HTMLElement {
  return h("span", { class: `badge ${cls}`.trim() }, text);
}

function placementSection(a: AnchorPlacement, live: LiveData | null): HTMLElement {
  const steps = (a.operation ?? "").split(/; then | \+ /).filter(Boolean);
  const missing = live?.anchors.find((x) => x.path === a.path)?.missingGrids ?? [];
  const skipped = a.skippedOperations ?? [];
  const grid = live?.objectLayer?.grid[a.path];
  return sub("Placement", [],
    kv([
      // The coordinate operations from the layer CRS to the output CRS, in order.
      ["coord. operations", steps.length ? h("ol", { class: "oplist" }, steps.map((s) => h("li", null, s))) : undefined],
      ["status", a.status === "ok" ? undefined : h("span", { class: `t-${a.status === "error" ? "err" : "warn"}` },
                                                     a.reason ?? a.status)],
      [labelHelp("grid → true", "The layer is projected, and ArcGIS takes a feature's model in the projection's " +
                 "grid: its north is grid north and its lengths are grid lengths. Placement works in the true " +
                 "east/north/up frame with physical lengths, so the model is turned by the meridian convergence " +
                 "(written as the crs:orientation heading) and scaled by 1 / the point scale, both at the feature's origin."),
       grid ? h("span", { class: "mono" }, `heading ${grid.meridianConvergence.toFixed(4)}°, ` +
                                           `scale 1/${grid.meridionalScale.toFixed(6)}`) : undefined],
      [labelHelp("accuracy", "The accuracy the engine reports for each coordinate operation used (from the " +
                 "EPSG dataset, e.g. a datum shift's), summed over the steps above. Not measured: conversions " +
                 "such as map projections are exact (±0 m), and one step without a stated accuracy makes it unknown."),
       a.status === "ok" ? h("span", { class: "mono" }, accuracyText(a.accuracy)) : undefined],
      ["skipped", skipped.length ? h("ul", { class: "oplist" }, skipped.map((s) => h("li", null, s)))
                                 : a.status === "ok" ? h("span", { class: "muted" }, "none") : undefined],
      ["grids", missing.length ? h("span", { class: "chips" }, missing.map((g) => {
        const fetched = live?.grids.includes(g);
        return h("span", { class: `chip ${fetched ? "ok" : "warn"}` },
                 `${g} · ${fetched ? "fetched" : "not served"}`);
      })) : undefined],
    ]));
}

function positionSection(origin: number[], global: boolean, crs: string | undefined): HTMLElement {
  return sub("Position", [h("span", { class: "muted small", style: "font-weight: 400" },
                            "in ", ...(crs ? epsgLinks(crs, "") : ["output CRS"]))],
    global
      ? kv([["lon, lat", h("span", { class: "mono" }, `${origin[0].toFixed(8)}, ${origin[1].toFixed(8)}`)],
            ["h", h("span", { class: "mono" }, `${origin[2].toFixed(3)} m`)]])
      : kv([["x, y", h("span", { class: "mono" }, `${origin[0].toFixed(3)}, ${origin[1].toFixed(3)}`)],
            ["z", h("span", { class: "mono" }, origin[2].toFixed(3))]]));
}

function meshesSection(meshes: { triangles?: number; linearizationError: number | null }[]): HTMLElement | null {
  if (!meshes.length) return null;
  const triangles = meshes.every((m) => m.triangles !== undefined)
    ? meshes.reduce((n, m) => n + (m.triangles ?? 0), 0) : undefined;
  const errors = meshes.map((m) => m.linearizationError).filter((e): e is number => e !== null);
  return sub("Meshes", [], kv([
    ["count", h("span", { class: "mono" }, meshes.length)],
    ["triangles", triangles === undefined ? undefined : h("span", { class: "mono" }, triangles.toLocaleString("en"))],
    [labelHelp("max lin. error", "Each mesh is drawn with one matrix: the coordinate operation linearized " +
               "at the anchor. This is the largest distance, over all of the meshes' vertices, between where " +
               "that matrix puts a vertex and where the full coordinate operation (F(x) per vertex) puts it, " +
               "in the anchor's frame (metres for an east-north-up frame)."), errors.length
      ? h("span", { class: "mono" }, `${(Math.max(...errors) * 1000).toFixed(3)} mm`) : undefined],
  ]));
}

function testSection(path: string, cases: { anchor: string; description: string; expect: string;
                                              expectBrowser?: string }[],
                     report: HarnessReport | null, live: boolean): HTMLElement | null {
  const tc = cases.find((c) => c.anchor === path);
  if (!tc) return null;
  const result = report?.cases.find((c) => c.anchor === path);
  const expected = live && tc.expectBrowser ? `${tc.expectBrowser} (browser; ${tc.expect} natively)` : tc.expect;
  const cls = result ? (!result.ok ? "err" : result.result === "pass" ? "ok" : "warn") : "";
  return sub("Test case", [], kv([
    ["description", tc.description],
    ["expected", expected],
    // Scored by the browser tests (__app.score); shown when a score exists.
    ["Score", result
      ? [badge(result.result, cls), " ", h("span", { class: "small muted" }, result.detail)]
      : undefined],
  ]));
}

/**
 * A CRS's id for headings: a compound as "<horizontal>+<vertical>" (e.g.
 * EPSG:4326+5773, as the output CRS is written, rather than EPSG:9707), else
 * its own id, else "<horizontal id> (3D)" for a promoted CRS.
 */
function crsCode(c: CrsDescription): string {
  const hid = c.horizontal?.id ?? "", vid = c.vertical?.id ?? "";
  if (c.type === "compound" && hid.startsWith("EPSG:") && vid.startsWith("EPSG:")) {
    return `${hid}+${vid.slice(5)}`;
  }
  return c.id || (hid ? `${hid}${c.dimension === 3 ? " (3D)" : ""}` : "");
}

interface CrsBlockOptions {
  /** Heights assumed (a layer without a vertical CRS). */
  assumed?: string;
  /** Vertical text instead of the vertical part's. */
  heights?: string;
  /** Id prefix for the vertical element (specs). */
  ids?: string;
  /** Show the WKT (default); the output CRS card only for a WKT definition. */
  wkt?: boolean;
  /** Where the CRS is defined (output CRS card). */
  definedBy?: string;
}

/** The "?" popover on a CRS card's title line: where the CRS comes from. */
function whyHelp(why: string): HTMLElement[] {
  return [h("button", { class: "help", type: "button", title: "Where this CRS comes from" }, "?"),
          h("span", { class: "pop" }, why)];
}

/**
 * One CRS, the same for the layer and the output CRS cards (code and name are
 * on the card's title line): horizontal, vertical, CRS type, where it is
 * defined (output), WKT (output: only when defined as WKT); CRS family after
 * CRS type.
 */
function crsBlock(c: CrsDescription, o: CrsBlockOptions = {}): HTMLElement {
  const heights: (Node | string)[] = o.heights ? [o.heights]
    : c.vertical?.id ? nameWithCode(c.vertical.name, c.vertical.id)
    : c.vertical ? [c.vertical.name]
    : c.dimension === 3 || /^geographic 3D|geocentric/.test(c.type ?? "") ? ["ellipsoidal"]
    : c.defaultHeight ? [...nameWithCode("EGM2008 height", c.defaultHeight), ", default for 2D"]
    : ["none"];
  return h("div", null,
    kv([
      ["horizontal", part(c.horizontal)],
      ["vertical", [h("span", { id: o.ids ? `${o.ids}Heights` : undefined }, ...heights),
                    o.assumed ? h("span", { class: "muted" }, " (assumed)") : null]],
      ["CRS type", c.type || undefined],
      ["CRS family", c.family || undefined],
      ["defined by", o.definedBy],
    ]),
    c.wkt && o.wkt !== false ? h("details", { class: "disclose", style: "margin-top: 3px" },
              h("summary", null, "WKT"), copyablePre(c.wkt)) : null);
}
