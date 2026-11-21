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

Findings for later milestones:
- emscripten 5.0.7 needs Python >= 3.10; `geo.py` sets `EMSDK_PYTHON` (AlmaLinux: `python3.12`).
- USD's WASM build uses `-fexceptions`; PROJ and SQLite must too, or PROJ's internal throws abort.
- Static PROJ embeds `proj.db` (`EMBED_RESOURCE_FILES`); the test binary is 13 MB. C4 trims the database.
- (C1) Without grids PROJ still offers a "Ballpark" operation (accuracy unknown) for e.g. NAD27 -> WGS84.
  The engine must disable ballpark operations (`proj_operation_factory_context_set_allow_ballpark`) so T3/T4
  report unsupported instead of placing metres off.

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
