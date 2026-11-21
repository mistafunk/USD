// Writes harness results as JSON and a self-contained HTML page to
// reports/<name>-<timestamp>.{json,html} and reports/latest.{json,html}.
// I1 adds its numeric checks (T5, T6, T9) as an extra table.
import { copyFileSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { CaseResult, HarnessReport } from "../src/harness";

/** A numeric check outside the control point harness (I1: T5, T6, T9). */
export interface ExtraCheck {
  group: string;
  name: string;
  ok: boolean;
  detail: string;
}

const esc = (s: string) => s.replace(/[&<>"]/g, (c) => `&#${c.charCodeAt(0)};`);
const metres = (v: number | null) => (v === null ? "–" : v.toFixed(4));

/** What placed a case (T2, T3, T11). */
function placed(c: CaseResult): string {
  if (c.operation === undefined) return "";
  const accuracy = c.accuracy === null || c.accuracy === undefined ? "unknown" : `${c.accuracy} m`;
  const skipped = c.skippedOperations?.length
    ? `; ${c.skippedOperations.length} more accurate operation(s) skipped for missing grids, ` +
      `e.g. ${esc(c.skippedOperations[0])}` : "";
  return `<br><small>${esc(c.engine ?? "")}: ${esc(c.operation)}; accuracy ${accuracy}${skipped}</small>`;
}

function extraTable(extra: ExtraCheck[]): string {
  if (!extra.length) return "";
  const rows = extra.map((c) =>
    `<tr class="${c.ok ? "ok" : "bad"}"><td>${c.ok ? "✓" : "✗"}</td><td>${esc(c.group)}</td>` +
    `<td>${esc(c.name)}</td><td>${esc(c.detail)}</td></tr>`).join("");
  return `<h2>Numeric checks</h2><table><thead><tr><th></th><th>Test</th><th>Check</th>` +
    `<th>Detail</th></tr></thead><tbody>${rows}</tbody></table>`;
}

function table(r: HarnessReport): string {
  const rows = r.cases.map((c) => {
    const points = c.points.map((p) =>
      `<tr class="pt"><td></td><td>${esc(p.name)}</td><td></td><td></td>` +
      `<td class="num">${metres(p.error)}</td><td class="num">${metres(p.horizontal)}</td>` +
      `<td class="num">${metres(p.vertical)}</td>` +
      `<td>${esc(p.note ?? "")}</td></tr>`).join("");
    return `<tr class="${c.ok ? "ok" : "bad"}"><td>${c.ok ? "✓" : "✗"}</td>` +
      `<td><b>${esc(c.anchor)}</b><br><small>${esc(c.description)}</small>${placed(c)}</td>` +
      `<td>${c.expect}</td><td class="${c.result}">${c.result}</td>` +
      `<td colspan="3"></td><td>${esc(c.detail)}</td></tr>${points}`;
  }).join("");
  const s = r.summary;
  return `<h2>${esc(r.view)} view, ${r.mode} <small>(${esc(r.output)}; ${esc(r.engine)}; ` +
    `tolerance ${r.tolerance} m)</small></h2>` +
    `<p><small>WebGL: ${esc(r.renderer ?? "unknown")}</small></p>` +
    `<p>${s.pass} pass, ${s.fail} fail, ${s.unsupported} unsupported, ` +
    `<b>${s.unexpected} unexpected</b></p>` +
    `<table><thead><tr><th></th><th>Case / control point</th><th>Expected</th><th>Result</th>` +
    `<th>3D error (m)</th><th>Horizontal</th><th>Vertical</th><th>Detail</th></tr></thead>` +
    `<tbody>${rows}</tbody></table>`;
}

export function writeReport(dir: string, reports: HarnessReport[], name = "w2",
                            extra: ExtraCheck[] = []): string {
  mkdirSync(dir, { recursive: true });
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  const unexpected = reports.reduce((n, r) => n + r.summary.unexpected, 0) +
    extra.filter((c) => !c.ok).length;
  const html = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${name.toUpperCase()} Harness Report</title><style>
body{font:14px/1.4 system-ui,sans-serif;margin:16px;max-width:1100px}
table{border-collapse:collapse;width:100%;margin-bottom:24px}
th,td{border-bottom:1px solid #ddd;padding:4px 6px;text-align:left;vertical-align:top}
td.num{font-family:ui-monospace,monospace;text-align:right}
tr.pt td{color:#555;font-size:12px}
tr.bad{background:#fde8e8} .pass{color:#11734b} .fail{color:#b42318} .unsupported{color:#8a5a00}
small{color:#666}
</style></head><body>
<h1>${name.toUpperCase()} harness report</h1>
<p>${new Date().toISOString()} · ${unexpected === 0 ? "all cases as expected" :
    `<b>${unexpected} unexpected result(s)</b>`}</p>
${reports.map(table).join("\n")}
${extraTable(extra)}
</body></html>
`;
  const json = JSON.stringify({ time: new Date().toISOString(), unexpected, reports, extra },
                             null, 1);
  const base = join(dir, `${name}-${stamp}`);
  writeFileSync(`${base}.json`, json);
  writeFileSync(`${base}.html`, html);
  copyFileSync(`${base}.json`, join(dir, "latest.json"));
  copyFileSync(`${base}.html`, join(dir, "latest.html"));
  return `${base}.html`;
}
