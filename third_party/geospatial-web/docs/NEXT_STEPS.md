# Next steps for Claude Code

State at handover (2026-11-21): branch `geospatial-web` = upstream v26.08 + these handover docs. No code yet.

**Update 2026-11-21 (Claude Code):** step 1 is in place: `geo.py`, `CMakePresets.json`, `cmake/sqlite3/`,
`requirements.txt` (see `third_party/geospatial-web/tools/README.md`). Pinned PROJ 9.9.0 and SQLite 3.54.1.
Verified on AlmaLinux 9: `bootstrap native --steps venv,sqlite,proj` and `bootstrap wasm32 --steps emsdk,sqlite,proj`
build; a WASM PROJ test under Node gives the same UTM 11N -> EPSG:4979 result as native `cs2cs`.
Still to run (long, in Simon's terminal): the `usd-deps` step for both targets, then `build native-debug`
and `build wasm32`. Container builds on the `emscripten/emsdk:5.0.7` image are not wired into `geo.py` yet.
All our non-upstream files now live in `third_party/geospatial-web` (was `scripts/geospatial` and
`extras/usd/examples/geospatialWeb`). When `module/` arrives, hook it into the root `CMakeLists.txt` next to
the RenderMan block, behind an option (one-line upstream-file edit, in its own commit).

**Update 2026-11-21, W0 spike (Claude Code):** `third_party/geospatial-web/app` (see its README). A RenderNode draws
three meshes (10 m box, 1 m box, 5 cm pole) at float32-exact origins in a global and a local (Web Mercator) SceneView.
Playwright (headless Chromium, software WebGL2) passes for both modes: `crossOriginIsolated` and SharedArrayBuffer true,
SDK and basemap load under COEP `require-corp` with no failed requests or console errors. Screenshots at overview,
street level and 1 m show clean geometry with the origin split; the naive float32 variant is visibly bent at street level
and displaced by more than a metre at 1 m. Still open for the W0 exit: an interactive look on a real GPU while moving the
camera (`geo.py serve dev`), which headless screenshots cannot show.

Findings for later milestones:
- emscripten 5.0.7 needs Python >= 3.10; `geo.py` sets `EMSDK_PYTHON` (AlmaLinux: `python3.12`).
- USD's WASM build uses `-fexceptions`; PROJ and SQLite must too, or PROJ's internal throws abort.
- Static PROJ embeds `proj.db` (`EMBED_RESOURCE_FILES`); the test binary is 13 MB. C4 trims the database.
- (C1) Without grids PROJ still offers a "Ballpark" operation (accuracy unknown) for e.g. NAD27 -> WGS84.
  The engine must disable ballpark operations (`proj_operation_factory_context_set_allow_ballpark`) so T3/T4
  report unsupported instead of placing metres off.
- (W0, affects C3/I1) **Global render space is not EPSG:4978.** The SDK docs: in global mode render coordinates are
  ECEF on a *sphere* of radius 6378137 m, not the WGS84 ellipsoid. Requesting EPSG:4978 as the output CRS and feeding
  that to the RenderNode would misplace data (up to ~21 km). Options: output geographic 3D (EPSG:4979) anchors plus
  ENU-relative matrices, and let the JS side map each anchor with `renderCoordinateTransformAt` in Float64; or have C++
  reproduce the SDK's sphere mapping (an SDK implementation detail). **Decided: the first option, D13.**
- (W0) **Local Web Mercator scenes are not metric.** `renderCoordinateTransformAt` in a local 3857 view returns an
  unscaled ENU frame (1 m = 1 Mercator unit), so metre-sized meshes render at cos(lat) of their size (0.68x in Zurich).
  Our C++ placement into the view SR (3857) will include the projection scale, which is correct; the W1 reference must
  do the same, and real local scenes are better in a conformal CRS with scale near 1 (UTM, LV95) with a matching basemap.
- (W0) The SDK's CDN worker is blocked under COEP `require-corp`; self-host the SDK assets (done in `app/`).
- (W0) The SDK does not know RenderNode geometry when choosing near/far clip distances; close-range views (T6) need
  `view.constraints.clipDistance` set explicitly.

Both tracks can start in parallel. Suggested order:

1. **Tooling first: `third_party/geospatial-web/tools/geo.py` and `third_party/geospatial-web/tools/CMakePresets.json`.**
   - Subcommands: `bootstrap native|wasm32`, `build <preset>`, `test <preset>`, `reference`, `serve dev|docker`.
   - Workspace root `USD_GEO_WS` (default `~/usd-geo`, `%USERPROFILE%\usd-geo` on Windows), `pathlib` everywhere.
   - `bootstrap native` wraps `build_usd.py` for native deps; `bootstrap wasm32` activates emsdk 5.0.7
     (or uses the `emscripten/emsdk:5.0.7` image via Podman/Docker) and runs `build_usd.py --build-target wasm`.
   - Both build PROJ + SQLite from source at one pinned version (pick the latest PROJ release and record it in `geo.py`).
   - Presets `native-debug`, `native-release`, `wasm32`, per-OS conditions (Ninja + GCC/Clang, Ninja + MSVC).
   - Check on AlmaLinux 9 first: GCC 11, CMake >= 3.30 from pip, Ninja from CRB/EPEL, Python 3.9 venv.
2. **W0 RenderNode spike** in `third_party/geospatial-web/app` (Vite, `@arcgis/core` pinned, COOP/COEP headers,
   hard-coded mesh at a float32-exact origin, global and local SceneView). Exit criteria in PLAN.md.
3. **C0 library skeleton**: `pxr/usd/usdGeospatial` schema.usda + `usdGenSchema`, `pxr_library`, ctest loading the
   proposal's examples. CMake finds PROJ only in the plugin.
4. **C2 WASM port** can start alongside C0; begin with step 1 in `WASM_GAP.md` (size the Autodesk diff).

Then C1 (resolution + engine registry + PROJ plugin), W1 (recorded data from the Python reference), W2 (harness),
C3, C4, I1, I2. See PLAN.md for exit criteria and the test plan T1 to T11.

Open questions to raise with Simon (or the proposal authors) are listed in PLAN.md.
