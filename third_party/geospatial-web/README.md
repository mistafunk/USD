# geospatial-web

Prototype: OpenUSD with geospatial CRS support, compiled to WASM and drawn on the ArcGIS Maps SDK for JavaScript.

Everything in this directory is specific to the prototype and is not meant for upstream OpenUSD. The upstreamable
parts live in the usual places: `pxr/usd/usdGeospatial`, `pxr/usd/plugin/usdGeospatialProj`, and the WASM port of
the GPU-free Hydra subset in `build_scripts/`, `cmake/`, `pxr/imaging` and `pxr/usdImaging`.

| Path | Contents |
|---|---|
| `docs/` | Plan, decisions, next steps, WASM gap analysis, proposal notes. Start with `docs/PLAN.md` |
| `tools/` | `geo.py` driver, CMake presets, SQLite build, nginx config. See `tools/README.md` |
| `module/` | embind module and final WASM link (C4) |
| `app/` | Maps SDK web app, RenderNode, worker, Playwright harness (W0 onwards) |
| `testdata/`, `reference/` | Test stages, control points, offline Python reference (W1) |

Quick start: `python third_party/geospatial-web/tools/geo.py check`.
