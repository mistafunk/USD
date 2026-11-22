// Panel chrome (D27): tabs, collapse, help popovers, status bar, Log tab,
// Diagnostics. Content of the Features and Layers tabs: features.ts, layers.ts.
import { formatBytes, type LoadStatus } from "../loadStatus";
import { copy, h } from "./dom";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** Shows a tab: "features" or "layers". */
export function showTab(name: string) {
  document.querySelectorAll<HTMLButtonElement>(".tabs button")
    .forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  document.querySelectorAll<HTMLElement>(".tab")
    .forEach((t) => t.classList.toggle("active", t.id === `tab-${name}`));
  // One scroll container for all tabs: start each at its top. The Layers tab
  // scrolls only its list of layers (style.css, .tabbody[data-tab]).
  const body = document.querySelector<HTMLElement>(".tabbody")!;
  body.dataset.tab = name;
  body.scrollTop = 0;
}

export function initPanel() {
  document.querySelectorAll<HTMLButtonElement>(".tabs button")
    .forEach((b) => b.addEventListener("click", () => showTab(b.dataset.tab!)));

  const collapse = $<HTMLButtonElement>("collapse");
  collapse.addEventListener("click", () => {
    const collapsed = $("panel").classList.toggle("collapsed");
    collapse.textContent = collapsed ? "+" : "−";
    collapse.title = collapsed ? "Expand panel" : "Collapse panel";
  });

  // Help popovers: the "?" button toggles the popover after it.
  const closeAll = () => {
    document.querySelectorAll(".pop.show").forEach((p) => p.classList.remove("show"));
    document.querySelectorAll(".help.open").forEach((b) => b.classList.remove("open"));
  };
  document.querySelectorAll<HTMLButtonElement>(".help").forEach((b) => {
    b.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      const pop = b.nextElementSibling!;
      const show = !pop.classList.contains("show");
      closeAll();
      pop.classList.toggle("show", show);
      b.classList.toggle("open", show);
    });
  });
  document.addEventListener("click", closeAll);
}

/** Status bar and Log tab, following a LoadStatus. */
export function bindLoadStatus(status: LoadStatus) {
  const phase = $("phase");
  const bar = $("progressBar");
  const fill = $("progress");
  const log = $("log");
  const usd = $<HTMLInputElement>("logUsd");
  let queued = false;
  let failedShown = false;

  const render = () => {
    queued = false;
    const d = status.downloads();
    const downloading = d.active > 0 && !status.done;
    phase.textContent = status.phase + (downloading
      ? ` - ${formatBytes(d.loaded)}${d.total ? ` of ${formatBytes(d.total)}` : ""}` : "");
    phase.className = `status${status.failed ? " failed" : status.done ? " ok" : ""}`;
    bar.className = `progress ${status.failed ? "failed" : status.done ? "done" : downloading && d.total ? "" : "busy"}`;
    fill.style.width = downloading && d.total ? `${(100 * d.loaded) / d.total}%` : "";
    const entries = status.log.filter((e) => usd.checked || e.level !== "usd");
    log.replaceChildren(...entries.map((e) => h("span", { class: e.level },
      h("span", { class: "t" }, `${e.t.toFixed(1)}s`), e.text, "\n")));
    log.scrollTop = log.scrollHeight;
    const warnings = status.log.filter((e) => e.level === "warn").length;
    const errors = status.log.filter((e) => e.level === "error").length;
    $("logSummary").textContent = `${status.log.length} lines` +
      (warnings ? ` · ${warnings} warning${warnings > 1 ? "s" : ""}` : "") +
      (errors ? ` · ${errors} error${errors > 1 ? "s" : ""}` : "");
    const count = $("logCount");
    count.hidden = !errors && !warnings;
    count.textContent = String(errors + warnings);
    count.style.color = errors ? "var(--err)" : warnings ? "var(--warn)" : "";
    // Each failure opens the log once (a later load can fail again).
    if (status.failed && !failedShown) ($("logBox") as HTMLDetailsElement).open = true;
    failedShown = status.failed;
  };
  status.onChange(() => {
    if (queued) return;
    queued = true;
    requestAnimationFrame(render);
  });
  usd.addEventListener("change", render);
  $("logCopy").addEventListener("click", (e) => copy(status.log.map((x) =>
    `${x.t.toFixed(1)}s ${x.level === "info" ? "" : `[${x.level}] `}${x.text}`).join("\n"), e.currentTarget as HTMLElement));
  render();
}

/** Diagnostics: refreshed while open. */
export function bindDiagnostics(rows: () => [string, string, string?][]) {
  const box = $<HTMLDetailsElement>("diagBox");
  const dl = $("diag");
  const refresh = () => {
    dl.replaceChildren(...rows().flatMap(([k, v, cls]) =>
      [h("dt", null, k), h("dd", { class: cls }, v)]));
  };
  setInterval(() => { if (box.open) refresh(); }, 250);
  box.addEventListener("toggle", refresh);
}
