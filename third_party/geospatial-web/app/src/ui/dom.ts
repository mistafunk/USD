// Minimal DOM building for the panel (no framework, D27).

type Child = Node | string | number | null | undefined | false;
type Attr = string | number | boolean | undefined | ((event: Event) => void);

/**
 * An element with attributes and children. `on<event>` attributes add
 * listeners, `true`/`false` set or skip boolean attributes, undefined is
 * skipped; children may be nested arrays, falsy children are skipped.
 */
export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K, attrs: Record<string, Attr> | null, ...children: (Child | Child[])[]
): HTMLElementTagNameMap[K] {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs ?? {})) {
    if (v === undefined || v === false) continue;
    if (typeof v === "function") el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? "" : String(v));
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : String(c));
  }
  return el;
}

/** Key-value rows for a <dl class="kv">; entries with an undefined value are skipped. */
export function kv(rows: [string, Child | Child[] | undefined][]): HTMLDListElement {
  return h("dl", { class: "kv" }, rows.filter(([, v]) => v !== undefined)
    .flatMap(([k, v]) => [h("dt", null, k), h("dd", null, ...(Array.isArray(v) ? v : [v]))]));
}

/** A <pre class="wkt"> with a copy button. */
export function copyablePre(text: string, cls = "wkt"): HTMLPreElement {
  const button = h("button", { class: "btn sm copy", type: "button" }, "copy");
  const pre = h("pre", { class: cls }, button, text);
  button.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    copy(text, button);
  });
  return pre;
}

/** Copies text to the clipboard and flashes "copied" on the button. */
export function copy(text: string, button: HTMLElement) {
  const label = button.textContent;
  const done = () => {
    button.textContent = "copied";
    setTimeout(() => { button.textContent = label; }, 1200);
  };
  if (navigator.clipboard) navigator.clipboard.writeText(text).then(done, done);
  else done();
}

/**
 * An EPSG identifier with links to epsg.io: "EPSG:4326" as one link, a
 * compound "EPSG:2229+6360" as a link per code. Other text stays as is.
 */
export function epsgLinks(id: string, cls = "mono"): (Node | string)[] {
  const m = /^EPSG:(\d+)(?:\+(\d+))?$/.exec(id);
  if (!m) return [id];
  const link = (code: string, text: string) => h("a", {
    class: `epsg ${cls}`.trim(), href: `https://epsg.io/${code}`, target: "_blank", rel: "noopener",
    title: `EPSG:${code} on epsg.io`,
    // Inside a <summary> or card: open the link, do not toggle or select.
    onclick: (e: Event) => e.stopPropagation(),
  }, text);
  return m[2]
    ? [link(m[1], `EPSG:${m[1]}`), "+", link(m[2], m[2])]
    : [link(m[1], id)];
}

