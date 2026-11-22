// W2 test harness: scores placed 3DOLs against their control points.
//
// For each control point: place its anchor-local position with the recorded
// anchor matrix and the view's render frame (as drawn), read the render
// position back with fromRenderCoordinates() as WGS84 lon/lat/h, and compare
// with the point's reference coordinate. A test case (one 3DOL) is
//   pass         every control point within tolerance,
//   fail         any control point outside it,
//   unsupported  the anchor could not be placed and says why (a reported
//                failure, which the test plan counts as correct behaviour).

import SpatialReference from "@arcgis/core/geometry/SpatialReference.js";
import { fromRenderCoordinates, toRenderCoordinates } from "@arcgis/core/views/3d/webgl.js";
import { multiply, transformPoint } from "../mat4";
import type { CaseResult, HarnessInput, HarnessReport, Outcome, PointResult } from "../scene";

const A = 6378137;
const E2 = 6.69437999014e-3;

function ecef([lon, lat, h]: number[]): number[] {
  const l = (lon * Math.PI) / 180, p = (lat * Math.PI) / 180;
  const n = A / Math.sqrt(1 - E2 * Math.sin(p) ** 2);
  return [(n + h) * Math.cos(p) * Math.cos(l), (n + h) * Math.cos(p) * Math.sin(l),
          (n * (1 - E2) + h) * Math.sin(p)];
}

/** 3D, horizontal and vertical (up at `ref`) distance in metres, WGS84. */
function errors(got: number[], ref: number[]): [number, number, number] {
  const a = ecef(got), b = ecef(ref);
  const d = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const l = (ref[0] * Math.PI) / 180, p = (ref[1] * Math.PI) / 180;
  const east = -Math.sin(l) * d[0] + Math.cos(l) * d[1];
  const north = -Math.sin(p) * Math.cos(l) * d[0] - Math.sin(p) * Math.sin(l) * d[1] +
    Math.cos(p) * d[2];
  const up = Math.cos(p) * Math.cos(l) * d[0] + Math.cos(p) * Math.sin(l) * d[1] +
    Math.sin(p) * d[2];
  return [Math.hypot(...d), Math.hypot(east, north), up];
}

export function score(input: HarnessInput): HarnessReport {
  const anchors = new Map(input.anchors.map((a) => [a.path, a]));
  const exact = !!input.exactPoint;
  const local = input.view.viewingMode === "local";
  const cases: CaseResult[] = input.testCases.map((tc) => {
    const a = anchors.get(tc.anchor);
    const expect = input.live && tc.expectBrowser ? tc.expectBrowser
      : local && !exact && tc.expectLocal ? tc.expectLocal : tc.expect;
    const base = {
      anchor: tc.anchor, description: tc.description, expect,
      ...(a?.status === "ok" ? {
        engine: a.engine, operation: a.operation, accuracy: a.accuracy ?? null,
        skippedOperations: a.skippedOperations ?? [],
      } : {}),
    };
    if (!a || a.status !== "ok" || !a.matrix) {
      const detail = a ? `${a.status}: ${a.reason ?? ""}` : "anchor missing from the data";
      return { ...base, result: "unsupported", ok: expect === "unsupported", detail,
               points: [] };
    }
    const frame = input.anchorFrame(tc.anchor);
    const toRender = frame ? multiply(frame, a.matrix) : null;
    const points = input.controlPoints.filter((cp) => cp.anchor === tc.anchor).map((cp) => {
      const name = cp.path.slice(cp.path.lastIndexOf("/") + 1);
      if (!toRender || !cp.local) {
        return { name, error: null, horizontal: null, vertical: null, note: "not placed" };
      }
      if (cp.reference.crs !== "EPSG:4979") {
        return { name, error: null, horizontal: null, vertical: null,
                 note: `reference CRS ${cp.reference.crs} not supported by the harness` };
      }
      let render = transformPoint(toRender, cp.local);
      if (exact) {
        // The exact output position, placed by the SDK like any geometry in
        // the output CRS.
        const p = input.exactPoint!(cp.path);
        const out = p && toRenderCoordinates(input.view, p, 0, input.outputSpatialReference!,
                                             new Float64Array(3), 0, 1);
        if (!out) {
          return { name, error: null, horizontal: null, vertical: null, note: "no exact point" };
        }
        render = Array.from(out);
      }
      const geo = fromRenderCoordinates(input.view, render, 0, new Float64Array(3), 0,
                                        SpatialReference.WGS84, 1);
      if (!geo) {
        return { name, error: null, horizontal: null, vertical: null,
                 note: "fromRenderCoordinates failed" };
      }
      const [e3, eh, ev] = errors(Array.from(geo), cp.reference.position);
      return { name, error: e3, horizontal: eh, vertical: ev };
    });
    const measured = points.filter((p) => p.error !== null);
    const worst = Math.max(...measured.map((p) => p.error!));
    const allMeasured = points.length > 0 && measured.length === points.length;
    const result: Outcome = allMeasured && worst <= input.tolerance ? "pass" : "fail";
    const detail = allMeasured
      ? `max ${worst.toFixed(4)} m over ${points.length} control points`
      : `${points.length - measured.length} of ${points.length} control points not measurable`;
    return { ...base, result, ok: result === expect, detail, points };
  });

  const summary = { pass: 0, fail: 0, unsupported: 0, unexpected: 0 };
  for (const c of cases) {
    summary[c.result]++;
    if (!c.ok) summary.unexpected++;
  }
  return {
    view: input.view.viewingMode,
    output: input.output,
    engine: input.engine,
    mode: exact ? "exact" : "matrix",
    tolerance: input.tolerance,
    time: new Date().toISOString(),
    cases,
    summary,
  };
}
