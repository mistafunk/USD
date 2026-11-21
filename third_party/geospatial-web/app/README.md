# app

Maps SDK web app: a `RenderNode` with a minimal WebGL2 mesh pass. It draws recorded placement data from the offline
reference (W1, `public/recorded/`, format in `../docs/RECORDED_FORMAT.md`) or the W0 hard-coded meshes; from I1 on, the
live WASM module's output.

```
npm ci
npm run dev          # http://localhost:5173  (or: python tools/geo.py serve dev)
npm test             # Playwright, headless Chromium on the GPU (or: python ../tools/geo.py test web)
npm run typecheck
```

Pinned: `@arcgis/core` 5.2.7, Vite 8.3.8, TypeScript 7.0.2, Playwright 1.65.1 (exact versions, `package-lock.json`).

| File | Purpose |
|---|---|
| `src/meshPass.ts` | WebGL2 pass in a `RenderNode`. Per mesh: float32-exact render origin + Float64 matrix; modelView = `viewMatrix` x translate(origin) x matrix in Float64, cast to float32 once |
| `src/usd/` | OpenUSD in a Web Worker: `worker.ts` owns the module and stage, `client.ts` is the promise API, `types.ts` the data |
| `c4.html`, `src/c4.ts` | C4 check page: C1 cases and the live scene chain in the browser vs pyproj (`tests/c4.spec.ts`) |
| `src/main.ts` | SceneView (`?view=global` or `local`), content (`?data=recorded` or `w0`), camera presets, status panel, `window.__app` test hooks |
| `src/recorded.ts` | Loads and places recorded data (D13); global frame from the SDK's own geographic-to-render mapping |
| `src/w0Scene.ts`, `src/shapes.ts` | W0 test meshes in Zurich |
| `src/harness.ts` | W2 harness: scores control points (pass, fail, unsupported) via `fromRenderCoordinates`; also the panel's Score button |
| `src/scene.ts`, `src/mat4.ts` | Shared scene type; Float64 matrix helpers |
| `public/recorded/` | Reference export (generated: `python ../tools/geo.py reference -- export`) |
| `vite.config.ts` | COOP/COEP headers; serves the SDK assets from our origin at `/arcgis-assets` |
| `tests/w0.spec.ts` | W0 checks and screenshots (`test-results/w0-<view>[-naive]-<preset>.png`) |
| `tests/w1.spec.ts` | W1 checks: anchors ok, mast on the building corner in render space; screenshots `w1-<view>-<preset>.png` |
| `tests/w2.spec.ts`, `tests/report.ts` | W2: runs the harness in both views, expects every case's outcome; writes `reports/w2-<time>.{json,html}` and `reports/latest.*` (git-ignored) |

URL options: `?view=global|local`, `&data=recorded|w0`, `&src=recorded|recorded-cpp` (recorded data from the reference
export or from the C++ observer, `geo.py record`), and for W0 `&naive=1`, which bakes absolute render coordinates into float32 vertices (what a
float32-only pipeline does) to show the jitter the origin split avoids.

Notes:
- Tests render WebGL on the GPU by default (all specs in about 30 s on an RTX 2080). `GEO_SOFTWARE_GL=1` forces
  SwiftShader (about 3 min); without a usable GPU, Chromium falls back to it by itself. `GEO_BROWSER_CHANNEL=chrome`
  drives the installed Google Chrome instead of Playwright's Chromium (on AlmaLinux Playwright downloads its
  Ubuntu 24.04 build, which works, GPU included). No X11 or Wayland specific flags. The panel and the W2 report show
  the WebGL renderer that was used.
- The SDK loads its assets (including a worker script) from `js.arcgis.com` by default. Under
  `COEP: require-corp` the worker is blocked (`ERR_BLOCKED_BY_RESPONSE`, no CORP header), so the assets are
  self-hosted and `esriConfig.assetsPath` points at them. Basemap tiles and other CORS fetches are fine.
- The SDK picks near/far clip distances from the scene it knows, which excludes RenderNode geometry; at street
  level it clips our meshes. The street and 1 m presets set `view.constraints.clipDistance` to near 0.1 m.
- The ground's default `stay-above` navigation constraint keeps the camera about 5 m up; W0 sets it to `none`.
