// A small combobox (D27): a text field with a ▾ button that lists all
// entries of a <datalist> (value and label), filtered while typing. The
// browser's own datalist popup only offers entries that contain the current
// text, so with a value in the field the others disappear.
import { h } from "./dom";

export function combobox(input: HTMLInputElement, list: HTMLDataListElement) {
  input.removeAttribute("list");
  input.setAttribute("autocomplete", "off");
  input.setAttribute("role", "combobox");
  input.setAttribute("aria-expanded", "false");
  const wrap = h("span", { class: "combo" });
  input.replaceWith(wrap);
  const toggle = h("button", { class: "combo-toggle", type: "button", tabindex: "-1",
                                title: "Show all", "aria-label": "Show all" }, "▾");
  const menu = h("div", { class: "combo-menu", role: "listbox", hidden: true });
  wrap.append(input, toggle, menu);
  let active = -1;
  let showingAll = false;

  const items = () => [...menu.querySelectorAll<HTMLElement>(".combo-item")];
  const close = () => {
    menu.hidden = true;
    input.setAttribute("aria-expanded", "false");
    active = -1;
  };
  const pick = (value: string) => {
    input.value = value;
    input.dispatchEvent(new Event("input", { bubbles: true }));
    close();
    input.focus();
  };
  const open = (all: boolean) => {
    const text = input.value.trim().toLowerCase();
    const options = [...list.options].map((o) => ({ value: o.value, label: o.label || "" }));
    showingAll = all || !text;
    const shown = showingAll ? options
      : options.filter((o) => `${o.value} ${o.label}`.toLowerCase().includes(text));
    menu.replaceChildren(...(shown.length ? shown : options).map((o) =>
      h("div", { class: `combo-item${o.value === input.value ? " current" : ""}`, role: "option",
                 "data-value": o.value,
                 // mousedown: before the field's blur closes the menu.
                 onmousedown: (e: Event) => { e.preventDefault(); pick(o.value); } },
        h("div", { class: "mono" }, o.value),
        o.label && o.label !== o.value ? h("div", { class: "small muted" }, o.label) : null)));
    menu.hidden = !options.length;
    input.setAttribute("aria-expanded", String(!menu.hidden));
    active = -1;
  };
  const highlight = (i: number) => {
    const all = items();
    if (!all.length) return;
    active = (i + all.length) % all.length;
    all.forEach((el, k) => el.classList.toggle("active", k === active));
    all[active].scrollIntoView({ block: "nearest" });
  };

  toggle.addEventListener("mousedown", (e) => e.preventDefault());
  // ▾ shows all entries (also when the list is open but filtered); a second
  // click closes it.
  toggle.addEventListener("click", () => {
    if (menu.hidden || !showingAll) open(true); else close();
    input.focus();
  });
  input.addEventListener("input", (e) => { if (e.isTrusted) open(false); });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      if (menu.hidden) open(true);
      highlight(active + (e.key === "ArrowDown" ? 1 : -1));
    } else if (e.key === "Enter" && !menu.hidden && active >= 0) {
      e.preventDefault();
      pick(items()[active].dataset.value!);
    } else if (e.key === "Escape" && !menu.hidden) {
      e.preventDefault();
      close();
    }
  });
  input.addEventListener("blur", close);
}
