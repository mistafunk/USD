# Decisions

All made by Simon Haegler in the planning project, 2026-11-21. Re-open only with him.

| # | Decision | Notes |
|---|---|---|
| D1 | Architecture **option A**: OpenUSD v26.08 in WASM including Hydra (hf, hd, hdsi, pxOsd, cameraUtil, geomUtil) and usdImaging. Reprojection in a C++ `HdSingleInputFilteringSceneIndex` after `UsdImagingStageSceneIndex`. A C++ `HdSceneIndexObserver` + embind feeds a minimal WebGL2 mesh pass in a Maps SDK `RenderNode`. | Option B (placement in JS) dropped. Option D (offline Python + pyproj) kept as ground truth. |
| D2 | **No three.js, no Needle**, no web render delegate. | |
| D3 | Implement the geospatial CRS proposal ([Tamrat-B/OpenUSD-proposals#1](https://github.com/Tamrat-B/OpenUSD-proposals/pull/1), pinned at 4120379f) in C++ on v26.08: `CoordinateReferenceSystem` (`uniform token crs:wkt`), `GeospatialCRSBindingAPI` (`double3 crs:position`, `double3 crs:orientation`). | Pin the proposal commit; keep property names in one tokens file. |
| D4 | **Engine plugins.** `usdGeospatial` defines `UsdGeospatialCoordinateOperationEngine` + `UsdGeospatialEngineRegistry` (PlugRegistry discovery; selection by `USDGEOSPATIAL_ENGINE` env setting, API call, or plugInfo priority). Core has no PROJ dependency. | |
| D5 | **PROJ first**, everywhere including the browser (PROJ + SQLite built from source with emscripten, trimmed `proj.db`, no curl/libtiff, no grids in browser). One pinned PROJ version for native and WASM. | Esri Projection Engine (`usdGeospatialEsriPE`, JS bridge to `projectOperator`) is follow-on milestone E1. |
| D6 | Branch `geospatial-web` on mistafunk/USD, from upstream `release` (v26.08, ee47c679). The old usdGeospatial prototype branch is disregarded. | A stale `geospatial-crs` branch may still exist on the fork; Simon deletes it by hand. |
| D7 | **Everything in the fork**, no separate web repo. Upstreamable code and app code in separate directories and separate commits. | See commit rules in `CLAUDE.md`. |
| D8 | **No GitHub CI.** All builds and tests run locally on Simon's machine, so he can observe the devops complexity. | Upstream workflows only trigger on push to `dev`/`release` and PRs to `dev`; leave them untouched. |
| D9 | Primary platform **AlmaLinux 9** (same as Pixar's v26.08 test platform: Alma 9.5, GCC 11.5, CMake 3.30, Python 3.9). Workspace reproducible on macOS and Windows. Podman on Alma; `emscripten/emsdk:5.0.7` image is the reference wasm build. | |
| D10 | Tools: JetBrains (CLion for C++, WebStorm for the web app, PyCharm for the reference), Chrome DWARF extension for WASM debugging, Vite dev server or nginx in a container with COOP/COEP. | |
| D11 | No time estimates in the plan. | |
| D12 | wasm32 first; wasm64 only if memory runs out. | Open question in PLAN.md, current default. |
