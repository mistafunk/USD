// Features tab (D27): the output CRS and, per anchor, its source CRS,
// placement, position, display offset, meshes and test case. Live data adds
// the module's CRS descriptions, fetched grids and geoid heights; recorded
// data shows what its manifest has.
import type { HarnessReport } from "../harness";
import type { LiveData } from "../live";
import type { RecordedAnchor } from "../recorded";
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
    const sub = $("loadedSub");
    if (layer) {
      title.textContent = layer.title;
      title.title = layer.url;
      const link = /^https?:/.test(layer.url)
        ? h("a", { href: layer.url, target: "_blank", rel: "noopener" }, "layer") : null;
      sub.replaceChildren(`${layer.features} feature${layer.features === 1 ? "" : "s"} · `,
        ...epsgLinks(layer.crs.replace(" (3D)", ""), ""), layer.crs.endsWith("(3D)") ? " (z ellipsoidal)" : "",
        ...(link ? [" · ", link] : []));
    } else if (!scene) {
      title.textContent = "loading…";
      sub.textContent = "";
    } else if (scene.details) {
      title.textContent = live ? "Test scene" : "Test scene (recorded)";
      sub.textContent = live ? "sites in several CRSs, with control points and expected outcomes"
                             : scene.name;
    } else {
      title.textContent = "Spike meshes";
      sub.textContent = "hard-coded meshes placed in JavaScript";
    }
  }

  private renderOutput({ global, scene, live }: FeaturesData) {
    const details = scene?.details;
    const crs = live?.output.crs ?? details?.output.crs ?? "";
    const frame = live?.output.frame ?? details?.output.frame;
    $("outCode").replaceChildren(...(crs ? epsgLinks(crs, "") : ["none"]));
    $("outName").textContent = live?.crs.output.name ?? "";
    $("outSummary").textContent = [crs, live?.crs.output.name].filter(Boolean).join(" ");
    const frameEl = $("outFrame");
    frameEl.hidden = !frame;
    frameEl.textContent = frame === "enu" ? "frame: ENU metres" : "frame: CRS units";
    $("outWhy").textContent = global
      ? "The global view's render space is ECEF on a 6378137 m sphere, not EPSG:4978. Anchors are " +
        "placed in geographic 3D WGS 84 with an east-north-up frame in metres, and the app maps them " +
        "through the SDK's own geographic-to-render function."
      : "A local view renders in its own spatial reference, so anchors are placed directly in it, " +
        "promoted to 3D with ellipsoidal heights. Web Mercator is not metric: placement includes its " +
        "scale, about 1/cos(latitude).";
    const wkt = live?.output.wkt || live?.crs.output.wkt || "";
    const wktBox = $("outWktBox") as HTMLDetailsElement;
    wktBox.hidden = !wkt;
    $("outWkt").replaceWith(Object.assign(copyablePre(wkt), { id: "outWkt" }));
  }

  private renderAnchors({ global, scene, live, report }: FeaturesData) {
    const list = $("anchorList");
    const details = scene?.details;
    const anchors = (live?.anchors as RecordedAnchor[] | undefined) ?? details?.anchors ?? [];
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

  private anchorRow(a: RecordedAnchor, global: boolean, scene: Scene | null,
                    live: LiveData | null, report: HarnessReport | null): HTMLElement {
    const details = scene?.details;
    const meshes = live
      ? live.meshes.filter((m) => m.anchor === a.path && m.status === "ok")
      : [...(details?.meshAnchor ?? new Map())].filter(([, anchor]) => anchor === a.path)
        .map(([path]) => ({ path, triangles: undefined, linearizationError: null }));
    const crs = live?.crs.anchors[a.path];
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
      h("span", { class: "path", title: a.path }, a.path),
      h("span", { class: "acc" }, placed ? accuracyText(a.accuracy, "±") : "n/a"),
      h("span", { class: "actions" }, solo, mute, zoom),
      h("span", { class: "meta" }, `${crs?.name || a.sourceCrs || "no source CRS"} · ` +
        `${meshes.length} mesh${meshes.length === 1 ? "" : "es"}`),
      // Two lines here (CSS); the full reason is in the Placement section.
      !placed && a.reason ? h("span", { class: "reason", title: a.reason }, a.reason) : null);
    const row = h("details", {
      class: `anchor s-${a.status}${this.selected === a.path ? " selected" : ""}` +
             `${this.soloed === a.path ? " soloed" : ""}${this.muted.has(a.path) ? " muted" : ""}`,
      "data-path": a.path, "data-status": a.status, open: this.open.has(a.path),
    }, summary, h("div", { class: "body" },
      sourceSection(a, crs),
      placementSection(a, live),
      placed && a.origin ? positionSection(a.origin, global, live?.output.crs ?? details?.output.crs) : null,
      displaySection(a.path, details?.displayOffsets),
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

function part(p: { name: string; id: string } | undefined) {
  return p ? [p.name, " ", ...epsgLinks(p.id, "mono muted")] : undefined;
}

function sub(title: string, badges: (HTMLElement | null)[], ...body: (Node | null)[]): HTMLElement {
  return h("div", { class: "sub" }, h("h4", null, title, ...badges), ...body);
}

function badge(text: string, cls = ""): HTMLElement {
  return h("span", { class: `badge ${cls}`.trim() }, text);
}

function sourceSection(a: RecordedAnchor, crs: CrsDescription | undefined): HTMLElement {
  if (!crs) {
    return sub("Source CRS", [], kv([["name", a.sourceCrs ?? (a.status === "ok" ? "" : "none")]]));
  }
  if (crs.error) {
    return sub("Source CRS", [badge("error", "err")], kv([["binding", crs.error]]));
  }
  const badges = [
    crs.type ? badge(crs.type) : null,
    crs.dimension === 2 ? badge("2D") : null,
    crs.defaultHeight ? badge("EGM2008 default", "warn") : null,
  ];
  const vertical = crs.vertical ? part(crs.vertical)
    : crs.defaultHeight ? ["EGM2008 height ", ...epsgLinks(crs.defaultHeight, "mono muted"),
                           " (default for 2D)"] : undefined;
  return sub("Source CRS", badges,
    kv([
      ["name", crs.name],
      ["id", crs.id ? h("span", null, ...epsgLinks(crs.id))
        : h("span", { class: "muted" }, ...(crs.horizontal?.id && crs.dimension === 3
          ? ["none (3D form of ", ...epsgLinks(crs.horizontal.id), ")"] : ["none"]))],
      ["horizontal", part(crs.horizontal)],
      ["vertical", vertical],
      ["area of use", crs.areaOfUse?.name || undefined],
    ]),
    crs.wkt ? h("details", { class: "disclose", style: "margin-top: 3px" },
                h("summary", null, "WKT"), copyablePre(crs.wkt)) : null);
}

function placementSection(a: RecordedAnchor, live: LiveData | null): HTMLElement {
  const steps = (a.operation ?? "").split(/; then | \+ /).filter(Boolean);
  const missing = live?.anchors.find((x) => x.path === a.path)?.missingGrids ?? [];
  const skipped = a.skippedOperations ?? [];
  return sub("Placement", [a.engine ? badge(`engine: ${a.engine}`) : null],
    steps.length ? h("ol", { class: "oplist" }, steps.map((s) => h("li", null, s))) : null,
    kv([
      ["status", a.status === "ok" ? undefined : h("span", { class: `t-${a.status === "error" ? "err" : "warn"}` },
                                                     a.reason ?? a.status)],
      ["accuracy", a.status === "ok" ? h("span", { class: "mono" }, accuracyText(a.accuracy)) : undefined],
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

function displaySection(path: string, offsets: Map<string, number> | undefined): HTMLElement | null {
  const n = offsets?.get(path);
  if (n === undefined) return null;
  return sub("Display", [], kv([["geoid offset", [h("span", { class: "mono" }, `${n.toFixed(2)} m`),
    `: drawn ${n.toFixed(2)} m lower, placement unchanged`]]]));
}

function meshesSection(meshes: { triangles?: number; linearizationError: number | null }[]): HTMLElement | null {
  if (!meshes.length) return null;
  const triangles = meshes.every((m) => m.triangles !== undefined)
    ? meshes.reduce((n, m) => n + (m.triangles ?? 0), 0) : undefined;
  const errors = meshes.map((m) => m.linearizationError).filter((e): e is number => e !== null);
  return sub("Meshes", [], kv([
    ["count", h("span", { class: "mono" }, meshes.length)],
    ["triangles", triangles === undefined ? undefined : h("span", { class: "mono" }, triangles.toLocaleString("en"))],
    ["max lin. error", errors.length
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
