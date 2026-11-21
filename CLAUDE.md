# OpenUSD geospatial web prototype (branch `geospatial-web`)

This fork branch (mistafunk/USD, `geospatial-web`, based on upstream `release` = v26.08, ee47c679)
builds a browser prototype that shows OpenUSD geospatial CRS support on the ArcGIS Maps SDK for JavaScript.

Everything else in this repository is upstream OpenUSD. Read these first:

- `third_party/geospatial-web/docs/PLAN.md`: the full plan (architecture, C++ design, tooling, milestones, test plan).
- `third_party/geospatial-web/docs/DECISIONS.md`: decisions already made. Do not re-open them without Simon.
- `third_party/geospatial-web/docs/NEXT_STEPS.md`: where to start.
- `third_party/geospatial-web/docs/WASM_GAP.md`: what upstream v26.08 does and does not build for WASM, and the Autodesk reference.
- `third_party/geospatial-web/docs/PROPOSAL_NOTES.md`: the parts of the geospatial proposal we implement.
- `third_party/geospatial-web/docs/RECORDED_FORMAT.md`: placement data the C3 observer must produce (W1 contract).

## Hard rules

1. **No GitHub CI.** Never add or edit anything under `.github/workflows`, never trigger Actions,
   never open a PR against `dev` (that would run upstream's BuildUSD workflow on the fork).
   All builds and tests run locally in the workspace, through `third_party/geospatial-web/tools/geo.py`.
2. **No three.js, no Needle, no Hydra render delegate for the web.** The renderer is a minimal
   WebGL2 mesh pass inside a Maps SDK `RenderNode`, fed by a C++ `HdSceneIndexObserver` via embind.
3. **usdGeospatial has no PROJ dependency.** Coordinate operations come from engine plugins
   (`UsdGeospatialCoordinateOperationEngine` found through `PlugRegistry`). PROJ (`usdGeospatialProj`)
   is the first engine, native and WASM. The Esri Projection Engine is a later alternative plugin (E1).
4. **Commit rules.** A commit touches either upstreamable paths (`pxr/`, `cmake/`, `build_scripts/`)
   or our own paths (`third_party/geospatial-web`, `CLAUDE.md`, `.claude/`),
   never both. WASM imaging fixes are small single-purpose commits so they can become upstream PRs.
5. **Portable workspace.** Simon works on AlmaLinux 9; it must also work on macOS and Windows.
   Python driver only (no bash/PowerShell-only scripts), `pathlib` paths, no symlinks or absolute paths
   in the repo, platform differences only in `geo.py` and CMake preset conditions.
6. **No time estimates** in plans, docs or replies.
7. Follow upstream OpenUSD code style for anything under `pxr/` (see `.clang-format`, existing libraries).

## Layout

| Path | Contents |
|---|---|
| `pxr/usd/usdGeospatial` | Schemas, binding resolution, placement F(x), engine interface + registry, geospatial scene index, observer |
| `pxr/usd/plugin/usdGeospatialProj` | PROJ engine plugin (option `PXR_BUILD_GEOSPATIAL_PROJ`) |
| `build_scripts/`, `cmake/`, `pxr/imaging/*`, `pxr/usdImaging/usdImaging` | WASM port of the GPU-free Hydra subset |
| `third_party/geospatial-web` | Everything of ours that never goes upstream: `docs/`, `tools/` (`geo.py`, `CMakePresets.json`, `nginx.conf`, compose file), `module/` (embind + final WASM link), `app/` (Maps SDK app, RenderNode, worker, Playwright harness), `testdata/`, `reference/` |

## Local workspace

Root from `USD_GEO_WS` (default: two levels above this clone, else `~/usd-geo`); the clone must sit at
`<workspace>/src/USD`. Contents: `emsdk/` (5.0.7), `venv/`, `venv-reference/`, `deps/<target>`, `build/<preset>`,
`install/<preset>`, `install/upstream-<target>` (unmodified build_usd.py reference), `cache/` (downloads, ccache,
emscripten cache, `logs/`). See `third_party/geospatial-web/tools/README.md`.
Presets: `native-debug`, `native-release`, `wasm32`. Driver: `python third_party/geospatial-web/tools/geo.py
check|bootstrap|build|test|reference|serve`; `test web [--ui]` runs the app's Playwright specs. Pins (emsdk, PROJ, SQLite) are at the top of `geo.py`.
The project venv (`<ws>/venv`) uses Python 3.12 (`VENV_PYTHON` in `geo.py`); the presets put it on PATH, so emcc
(needs Python >= 3.10) also works when CLion configures. AlmaLinux's system Python is 3.9.

Long builds (the WASM imaging build in particular) run in Simon's terminal so he can watch them.
Prefer telling him the `geo.py` command over running a long build silently.

## Subagents

`.claude/agents/` defines `build-runner` (runs `geo.py`, summarises failures) and `wasm-porter`
(ports one Autodesk fix at a time onto v26.08). Use them when a task fits; keep scope narrow.
