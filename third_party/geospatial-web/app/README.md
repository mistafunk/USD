# app

Maps SDK web app: a `RenderNode` with a minimal WebGL2 mesh pass. W0 draws hard-coded meshes; from W1 on it draws
recorded buffers, and from I1 the live WASM module's output.

```
npm ci
npm run dev          # http://localhost:5173  (or: python tools/geo.py serve dev)
npm test             # Playwright, headless Chromium with software WebGL2
npm run typecheck
```

Pinned: `@arcgis/core` 5.2.7, Vite 8.3.8, TypeScript 7.0.2, Playwright 1.65.1 (exact versions, `package-lock.json`).

| File | Purpose |
|---|---|
| `src/meshPass.ts` | WebGL2 pass in a `RenderNode`. Per mesh: float32-exact render origin + Float64 matrix; modelView = `viewMatrix` x translate(origin) x matrix in Float64, cast to float32 once |
| `src/main.ts` | SceneView (`?view=global` or `local`), test meshes, camera presets, status panel, `window.__w0` test hooks |
| `src/shapes.ts`, `src/mat4.ts` | Test geometry in local ENU metres; Float64 matrix helpers |
| `vite.config.ts` | COOP/COEP headers; serves the SDK assets from our origin at `/arcgis-assets` |
| `tests/w0.spec.ts` | W0 checks and screenshots (`test-results/w0-<view>[-naive]-<preset>.png`) |

URL options: `?view=global|local`, `&naive=1` bakes absolute render coordinates into float32 vertices (what a
float32-only pipeline does) to show the jitter the origin split avoids.

Notes:
- The SDK loads its assets (including a worker script) from `js.arcgis.com` by default. Under
  `COEP: require-corp` the worker is blocked (`ERR_BLOCKED_BY_RESPONSE`, no CORP header), so the assets are
  self-hosted and `esriConfig.assetsPath` points at them. Basemap tiles and other CORS fetches are fine.
- The SDK picks near/far clip distances from the scene it knows, which excludes RenderNode geometry; at street
  level it clips our meshes. The street and 1 m presets set `view.constraints.clipDistance` to near 0.1 m.
- The ground's default `stay-above` navigation constraint keeps the camera about 5 m up; W0 sets it to `none`.
