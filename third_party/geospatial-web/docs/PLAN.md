# OpenUSD Geospatial × Maps SDK Prototype Plan

Nov 21, 2026 · @Simon Haegler

## Goal and scope

Build a small browser app that loads a USD stage composed from two or more geo-referenced layers (3DOLs) in different spatial references, places the composed result correctly on a Maps SDK SceneView, and proves the placement against surveyed control points.

**In scope**

- A browser app on the ArcGIS Maps SDK for JavaScript: a RenderNode with a minimal WebGL2 mesh pass, fed by OpenUSD compiled to WASM and loaded separately from the SDK.
- A C++ implementation of the [geospatial CRS proposal](https://github.com/Tamrat-B/OpenUSD-proposals/pull/1) on OpenUSD v26.08 (latest stable release): the `GeospatialCRS` and `GeospatialCRSBindingAPI` schemas, binding resolution, placement evaluation and visible failures, with coordinate operations from pluggable engines (PROJ first), built natively and for WASM.
- Reprojection of each 3DOL in a Hydra scene index during composition, resolved in double precision.
- A WASM build of Hydra and usdImaging on v26.08, guided by the Autodesk fork's changes.
- An automated check: two 3DOLs vs control points with a stated tolerance, and explicit failure reports for transforms the engine cannot do.
- Added during the work: vertical CRSs with grids, native and fetched on demand in the browser (D23), and ArcGIS 3D object layers loaded through generated wrapper layers with Adobe's glTF plugin (D24).

**Not in scope**

- three.js, Needle, or any Hydra render delegate for the web; a scene index observer feeds the RenderNode directly.
- The Esri engine plugin (a follow-on, see E1), coordinate epochs, and materials beyond a basic shaded surface.
- The proposal's `GeospatialDataSource` measurement association, Profiles dependency claims and WKT normalization checks; these follow once placement works.

## Building blocks

At the start every piece existed except the WASM build of Hydra and usdImaging, and nothing connected OpenUSD to the Maps SDK. The prototype's real work was that port plus the glue: CRS resolution in a scene index, double-precision placement, and a mesh pass inside a RenderNode.

| Piece | What it gives us | Constraint that matters |
| --- | --- | --- |
| [Geospatial CRS proposal (PR #1)](https://github.com/Tamrat-B/OpenUSD-proposals/pull/1) | `GeospatialCRS` prim with `uniform token crs:wkt` (WKT2), `GeospatialCRSBindingAPI` with `double3 crs:position` and `double3 crs:orientation` (heading, pitch, roll vs local ENU), binding inherited down the hierarchy | Specifies data and observable behaviour, not APIs; the engine picks operations and must report failure, operation and accuracy |
| [OpenUSD v26.08](https://github.com/PixarAnimationStudios/OpenUSD/releases/tag/v26.08) | Native and WASM builds of `pxr/usd` (wasm32 and wasm64 via `build_usd.py --build-target wasm`, CI-tested since v25.11); `plugInfo.json` embedded under `/usd` | WASM: static libs only, pthreads, no Python; `build_usd.py` refuses `--usd-imaging`, and Hydra (`--imaging`) is never built in CI |
| [`geospatial-web` branch](https://github.com/mistafunk/USD/tree/geospatial-web) | Our branch on the mistafunk/USD fork, at upstream `release` (v26.08, commit ee47c67) | Clean start: the library and the WASM port are written fresh |
| [Autodesk USD fork](https://github.com/autodesk-forks/USD) | Working emscripten patches for Hydra and usdImaging on `adsk/feature/wasm` (26.05 dev, last synced March 2026); a fresh `adsk/hgiWebGPU` on 26.08 dev | One release behind; we cherry-pick the imaging build fixes only, not its WebGPU backend |
| [PROJ](https://proj.org/) | First engine plugin, natively and in WASM: WKT2 parsing, operation selection, accuracy, grids, ENU (topocentric) conversion | Needs SQLite, libtiff and `proj.db` (about 9 MB in full) built for WASM; grids are fetched on demand in the browser (D23) |
| [Maps SDK `RenderNode`](https://developers.arcgis.com/javascript/latest/references/core/views/3d/webgl/RenderNode/) | Custom WebGL2 pass in SceneView: `consumes`/`produces`, `render()`, `camera.viewMatrix`/`projectionMatrix`, `requestRender()` | Shares the SDK's GL context and FBOs; must restore GL state |
| [`webgl` utilities](https://developers.arcgis.com/javascript/latest/references/core/views/3d/webgl/) | `toRenderCoordinates`, `renderCoordinateTransformAt`, `fromRenderCoordinates` | Render space is ECEF on a 6378137 m sphere (global, not EPSG:4978) or the view SR (local); float32 jitter unless we subtract a local origin ([windmills sample](https://developers.arcgis.com/javascript/latest/sample-code/custom-render-node-windmills)) |
| [`projectOperator`](https://developers.arcgis.com/javascript/latest/references/core/geometry/operators/projectOperator/) + [`geographicTransformationUtils`](https://developers.arcgis.com/javascript/latest/references/core/geometry/operators/support/geographicTransformationUtils/) | Client-side Esri Projection Engine (WASM): `projectLoad()`, `projectExecuteMany()`, `getTransformations()` | Alternative engine plugin in E1. Equation-based datum transforms only; grid-based and vertical CRS projection unsupported |

## Architecture options

Option A is chosen: OpenUSD composes and reprojects inside its own Hydra scene index chain in WASM, and a minimal WebGL2 pass in a RenderNode draws the result. Neither three.js nor Needle is used.

| Option | Composition | Where reprojection happens | Renderer | Status |
| --- | --- | --- | --- | --- |
| **A. Hydra in WASM** | OpenUSD v26.08 WASM build with Hydra and usdImaging ported | Geospatial filtering scene index in C++, calling a coordinate operation engine | Minimal WebGL2 mesh pass in a RenderNode, fed by a C++ scene index observer | **Chosen** |
| B. Compose in WASM, place in JS | OpenUSD WASM | JS stage using `projectOperator` | A JS renderer | Dropped: throwaway placement code outside the proposal's model |
| D. Offline reference | Python OpenUSD + pyproj | Offline, per vertex | None | Kept as ground truth for tests |

Option A needs the most up-front work, mostly porting Hydra and usdImaging to WASM (milestone C2). In return, reprojection sits in the scene index, where Tamrat suggested it, and no JS placement code has to be thrown away later.

## Chosen approach

All geospatial work happens in C++ inside the WASM module. OpenUSD composes the stage, and a geospatial scene index resolves each anchor into the output CRS in Float64. JS maps each anchor into render space in Float64, uploads float32 buffers and draws them in a RenderNode (D13).

```
USD layers (3DOLs, each bound to a CRS)
  -> UsdImagingStageSceneIndex (compose)
  -> geospatial filtering scene index (check support via engine plugin; place F(x) in Float64, per-3DOL anchor + matrix)
       \-> unsupported / broken binding: error data source, reported, not drawn
  -> HdSceneIndexObserver + embind (typed arrays)
  -> Maps SDK RenderNode (WebGL2 mesh pass)
```

Only a 3DOL whose source-to-output transform the engine can perform reaches the placement step. Everything else is reported, not drawn.

**Data flow**

1. **Load.** A Web Worker loads our WASM build of v26.08 with usdGeospatial. JS preloads the USD files into the in-memory file system and opens the root layer. The app passes the output CRS (requirement 16): geographic 3D (EPSG:4979) for a global SceneView, whose render space is a sphere the SDK maps to (D13), or the view's spatial reference for a local one. A session layer is used only to override source bindings in tests.
2. **Compose.** OpenUSD composes sublayers, references and payloads. `UsdImagingStageSceneIndex` turns the stage into Hydra scene index prims.
3. **Check support.** For each direct CRS binding, the geospatial scene index asks the engine whether it can transform source to output. The engine is whichever engine plugin is active, PROJ to start with, in the browser as well as natively. If no usable transform exists (for example a grid that is not available), the prim gets an error data source and is reported as unsupported, with the missing grids named.
4. **Place.** The geospatial scene index (an `HdSingleInputFilteringSceneIndex`) evaluates the proposal's placement F(x). It publishes, per 3DOL, an anchor in the output CRS and a Float64 matrix from mesh-local units to that anchor's frame: ENU metres for a global view, view-SR units for a local one.
5. **Observe and render.** A C++ `HdSceneIndexObserver` collects mesh points, indices and normals plus the placement matrices. embind exposes them as typed arrays, which the worker transfers to the main thread. JS turns each anchor into a render frame (global: the derivative of the SDK's geographic-to-render mapping via `toRenderCoordinates`; local: a translation in view-SR units; see `RECORDED_FORMAT.md`), splits off a float32-exact origin, and the RenderNode draws with modelView = `camera.viewMatrix` x translate(origin) x frame x matrix, computed in Float64 before casting.

**Precision strategy**

- All CRS math and matrix products stay in C++ double (`GfMatrix4d`); only vertex buffers and the final per-draw matrix are float32. This matches the proposal's two-tier precision (double placement, `point3f` detail).
- One render origin per 3DOL, chosen float32-exact near its centre, following the SDK's [windmills sample](https://developers.arcgis.com/javascript/latest/sample-code/custom-render-node-windmills).
- A placement matrix per 3DOL is the default. A measurement mode applies the full point map F(x) to every vertex, so we can see what the matrix shortcut costs.

**Hosting**

Serve with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (or `credentialless`), because the full OpenUSD WASM builds need `SharedArrayBuffer`. Basemaps, the SDK's assets and ArcGIS services must then load with CORP or CORS; W0 settled this by self-hosting the SDK's assets.

## C++ CRS support on OpenUSD v26.08

We implement the proposal's initial scope as a `usdGeospatial` library on the geospatial-web branch (v26.08). Coordinate operations come from engine plugins: PROJ first, natively and in the browser, with the Esri Projection Engine added later as an alternative plugin.

| Part | What we build | Proposal reference |
| --- | --- | --- |
| Schemas | `GeospatialCRS` (typed, `crs:wkt`; D14) and `GeospatialCRSBindingAPI` (`crs:position`, `crs:orientation`), generated with `usdGenSchema` as a `pxr_library`, so WASM builds embed its `plugInfo.json` | Detailed design; principle 6, minimal disruption |
| Binding resolution | Direct vs inherited binding, nearest-ancestor walk; a broken direct binding is an error, with no fallback to the parent | CRS binding and inheritance |
| Validation | `UsdValidator` checks: missing or non-finite position, time-sampled uniform values, incomplete 3D CRS, unsupported axis directions, 2D CRS defaulting to EGM2008 height with a warning | Functional requirements; Observable outcomes |
| Placement evaluation | F(x): position to geographic 3D, ellipsoid-normal ENU basis, heading/pitch/roll, `metersPerUnit` and `upAxis` mapping, then the anchor's ordinary xformOps; time-sampled position and slerped orientation | Source placement evaluation and transform order |
| Engine interface | One abstract `CoordinateOperationEngine`: transform points between two WKT CRSs; return the operation used, accuracy (or unknown) and any failure reason | Principle 7; requirements 21 and 28 |
| Engine backends | usdGeospatialProj plugin first (native and WASM); usdGeospatialEsriPE plugin later as an alternative. See Transformation engine plugins below | Initial scope: the engine chooses operations |
| Geospatial scene index | In `usdGeospatialImaging` (D16). `HdSingleInputFilteringSceneIndex` after `UsdImagingStageSceneIndex` (as a usdImaging scene index plugin, after flattening): resolves each anchor into the requested output CRS in double precision, emits an anchor plus an anchor-relative matrix (D13), and attaches error data sources for failures | Queries, scene charts and instances; Tamrat's suggestion |
| Scene index observer | In `usdGeospatialImaging` (D16). `HdSceneIndexObserver` that keeps mesh buffers, matrices and status per prim and reports dirty ranges; replaces a render delegate | No render delegate needed |
| JS API (embind) | Open stage, set output CRS and time, read anchors and mesh buffers as typed arrays, resolve points, CRS WKT, geoid heights, reset engines after fetching grids | Not in the proposal; prototype only |
| Query API | Resolve a prim or point into an output CRS, returning coordinates, operation and accuracy | Runtime coordinate transformation |

### Transformation engine plugins

usdGeospatial defines the engine interface but contains no engine. PROJ and, later, the Esri Projection Engine are separate USD plugins that register against it, so the core library has no PROJ dependency and engines can be swapped without code changes.

- **Interface.** `UsdGeospatialCoordinateOperationEngine`, an abstract base with a `TfType` and a factory, like `ArResolver` or `HdSceneIndexPlugin`. Calls: `CanTransform(src, dst, area)` reporting the operation it would use for an area of interest, `Transform(src, dst, area, points)` returning points, the operation used, accuracy (or unknown), any failure reason, skipped better operations and missing grids, `DescribeCrs`, and `MakeCompoundCrs` for the EGM2008 default height of 2D CRSs. It is synchronous, because scene index queries are.
- **Registry.** `UsdGeospatialEngineRegistry` finds engines through `PlugRegistry` from each plugin's `plugInfo.json` (`Types` entry with `bases: ["UsdGeospatialCoordinateOperationEngine"]`), loads them on first use, and picks one by a `TfEnvSetting` (`USDGEOSPATIAL_ENGINE`), an explicit API call, or a priority in `plugInfo.json`. The chosen engine's name is stamped on every result, as the proposal requires.
- **usdGeospatialProj.** First engine, a separate plugin library linking PROJ. It maps WKT2 to `proj_create`, picks operations with `proj_create_operations`, reports accuracy from `proj_coordoperation_get_accuracy`, and builds the ENU basis with PROJ's topocentric conversion.
- **usdGeospatialEsriPE (follow-on, E1).** Second engine with the same interface. In the browser it calls the Maps SDK's `projectOperator` through an emscripten JS bridge, so it covers equation-based transforms only. A native build against Esri's Projection Engine SDK is possible later.
- **WASM.** No `dlopen`, so engine plugins are linked statically and register at static-init time. Their `plugInfo.json` is embedded under `/usd` like any `pxr_library`. Choosing an engine works the same way as natively.
- **PROJ for WASM.** PROJ, SQLite and libtiff built with emscripten, without curl and with network grids off. The full `proj.db` is embedded compressed; trimming it to the demo CRSs is left for I2 (D17). Grids are files the worker fetches on demand (D23).

### WASM support for Hydra and usdImaging

Upstream v26.08 builds `pxr/usd` for WASM but stops before Hydra. We closed that gap on geospatial-web in C2, using the Autodesk fork's `adsk/feature/wasm` branch as the reference rather than merging it. The steps, kept as the record of what the port consists of:

1. **Extract the reference diff.** Diff `adsk/feature/wasm` against upstream 26.05, limited to `build_scripts/`, `cmake/`, `pxr/imaging/{hd,hdar,hdsi,hf,hio,pxOsd,cameraUtil,geomUtil}` and `pxr/usdImaging/usdImaging`. Drop everything for WebGPU, Hgi, hdSt and Python. Done 2026-11-21, results in `WASM_GAP.md`.
2. **build\_usd.py.** Allow `--imaging` and `--usd-imaging` for `wasm` and `wasm64` targets, with GL, Vulkan, Metal, Embree, PTex, OpenVDB, OpenImageIO, OCIO and MaterialX off. OpenSubdiv already builds statically for WASM there.
3. **CMake.** Build only the GPU-free subset: hf, hd, hdar, hdsi, hio, pxOsd, cameraUtil, geomUtil, usdImaging. Skip hdSt, hgi\*, hdx, usdImagingGL and the tools.
4. **Static registration.** With static libs only, keep `TF_REGISTRY_FUNCTION` and scene index plugin registrations from being dropped by the linker. Use whole-archive linking or explicit anchors, and check that Hydra's plugin registry finds the embedded `plugInfo.json` entries.
5. **Threading.** Cap the WorkDispatcher and TBB thread counts to the emscripten pthread pool, and keep `PROXY_TO_PTHREAD` off so the worker owns the stage.
6. **Code fixes.** Apply the Autodesk fixes that still matter on v26.08, mostly `__EMSCRIPTEN__` guards in arch and hd and file-system assumptions, as small, separate commits.
7. **Local check.** Run a local wasm32 build with `--usd-imaging` and run the hd, hdsi and usdImaging ctests headless under Node, from one script in the local workspace.

**Native build.** Linux and macOS with PROJ, used for ctest and usdview checks. The browser build links the same PROJ plugin, built for WASM.

## Tooling setup

Everything lives in the OpenUSD fork on the geospatial-web branch. Upstreamable C++ and the prototype app sit in separate directories and separate commits, so the C++ can still be split into upstream PRs later.

| Path in [mistafunk/USD](https://github.com/mistafunk/USD/tree/geospatial-web) | Holds | Upstream later? |
| --- | --- | --- |
| `pxr/usd/usdGeospatial` | Schemas, binding resolution, placement, engine interface and registry | Yes, once the proposal is accepted |
| `pxr/usdImaging/usdGeospatialImaging` | usdImaging adapter, geospatial placement scene index, observer (D16) | Yes, with usdGeospatial |
| `pxr/usd/plugin/usdGeospatialProj` | PROJ engine plugin, behind a `PXR_BUILD_GEOSPATIAL_PROJ` option (later `usdGeospatialEsriPE` next to it) | Possibly |
| `build_scripts/`, `cmake/`, `pxr/imaging/*`, `pxr/usdImaging/usdImaging` | WASM port of Hydra and usdImaging | Yes, as separate PRs |
| `third_party/adobe-usd-gltf` | Adobe's glTF file format plugin (Apache-2.0), for glb models of ArcGIS 3D object layers (D24) | No (Adobe's project) |
| `third_party/geospatial-web/module` | `geospatialRecord` (C3: the observer's output in the recorded format), `geospatialModule` (C4: embind module and final link) (like upstream's `wasmFetchResolver` sample), built from the root `CMakeLists.txt` behind an option, like `third_party/renderman` | No |
| `third_party/geospatial-web/app` | The Maps SDK app, RenderNode, worker wrapper, `package.json`, Playwright harness | No |
| `third_party/geospatial-web/testdata` and `/reference` | Test stages, control points, offline Python reference (option D) | No |
| `third_party/geospatial-web/tools` | `geo.py` driver, CMake presets, nginx config. No GitHub workflows. | No |
| `third_party/geospatial-web/docs` | Plan, decisions, next steps and notes | No |

**Commit rules.** Each commit touches either upstreamable paths or the app paths, never both. WASM imaging fixes are small, single-purpose commits, so each can be cherry-picked into an upstream PR.

| Tool | Pin | Used for |
| --- | --- | --- |
| Emscripten SDK | 5.0.7, the version upstream CI uses | All WASM builds |
| CMake and Ninja | Versions required by OpenUSD v26.08 | All builds, including the example's embind link |
| PROJ | Built from source at one pinned version for native and WASM, with SQLite and libtiff, no curl; pinned grids | usdGeospatialProj |
| Python 3.12 with usd-core and pyproj | `venv-reference`, pyproj built against the workspace's PROJ (D19) | Offline reference and fixture generation |
| Node.js LTS, npm, Vite | Vite dev server configured to send COOP/COEP headers | Web app and local serving |
| `@arcgis/core` | Exact version in `package.json` | Maps SDK |
| Playwright (Chromium) | Exact version in `package.json` | Browser test harness |
| ccache and emscripten cache | In the workspace's `cache/` directory | Fast local rebuilds, especially of the WASM imaging build |

**No GitHub CI.** Every build and test runs on Simon's machine in one local workspace, driven by the scripts in `third_party/geospatial-web/tools`, so each devops step is visible and repeatable.

### Local workspace

The workspace layout, the driver's commands and the first-run sequence are in `tools/README.md`; the development
section of `README.md` is the short version for collaborators. The principles:

1. **One portable driver.** All steps go through `third_party/geospatial-web/tools/geo.py` (Python, like `build_usd.py`), with no shell-specific scripts, and all paths are built with `pathlib`. The workspace root comes from `USD_GEO_WS`, else two levels above the clone.
2. **Bootstrap once per target.** `geo.py bootstrap native|wasm32` builds SQLite, libtiff, PROJ, the pinned grids and tinygltf from source at the same pinned versions for both targets, so native and browser results come from identical PROJ code (test T9), then runs `build_usd.py` for USD's own dependencies.
3. **Iterate with CMake presets.** `tools/CMakePresets.json` defines `native-debug`, `native-release` and `wasm32`. The driver and CLion use the same presets and build trees, so a build started from either is the same build.

### Platforms

AlmaLinux 9 is the primary platform. It is also the Linux that Pixar tests v26.08 on (AlmaLinux 9.5, GCC 11.5, CMake 3.30, Python 3.9), according to `VERSIONS.md`.

|  | AlmaLinux 9 (now) | macOS (later) | Windows (later) |
| --- | --- | --- | --- |
| Compiler | GCC 11 from the base repos (`dnf groupinstall "Development Tools"`) | Xcode 16 command line tools | Visual Studio 2022 |
| CMake, Ninja | CMake 3.30 from pip or Kitware (the base repo's 3.26 is older than Pixar's tested version), Ninja from CRB/EPEL | Homebrew | Visual Studio's bundled CMake and Ninja, or pip |
| Python | Python 3.12 (AppStream) venv; system Python 3.9 unused | Homebrew Python plus a venv | python.org installer plus a venv |
| emsdk 5.0.7 | Native install, or the `emscripten/emsdk:5.0.7` image | Native install, or the image | The image (recommended), or WSL2 |
| Containers | Podman (default on AlmaLinux; `podman` and `podman compose` take the same commands as Docker) or Docker CE | Docker Desktop | Docker Desktop with WSL2 |
| usdview | Mesa GL and X11 development packages, PySide6 from pip | PySide6 from pip | PySide6 from pip |

Rules that keep the workspace portable:

- No symlinks or hard-coded absolute paths in the repo.
- Keep the workspace on a short path on Windows, such as `C:\usd-geo`, because of USD's long paths, and enable long-path support in Git.
- Platform differences live only in `geo.py` and the preset conditions, so a collaborator runs the same subcommands on any OS.

### JetBrains setup

- **CLion** for all C++. Open `src/USD` and load the presets as CMake profiles, using Ninja and ccache. Exclude `build/`, `deps/` and unused backends (hdSt, hgiVulkan, hgiMetal, usdview Python) from indexing. Run usdGeospatial tests through CLion's CTest integration and debug natively with LLDB or GDB. The `wasm32` preset finds the workspace's emsdk and venv itself.
- **WASM debugging** happens in Chrome, not CLion. Build `wasm32` with `-g` and use the C/C++ DevTools Support (DWARF) extension to step through C++ in the browser.
- **WebStorm** for `third_party/geospatial-web/app`: npm scripts, Vite, Playwright run configurations, and the JS debugger attached to Chrome.
- **PyCharm** for the offline reference: a virtualenv with usd-core and pyproj.

### Working with Claude

Claude Code runs in CLion's terminal from `src/USD`, so its edits show up as IDE diffs and every command it runs is visible. The root `CLAUDE.md` and the docs in `third_party/geospatial-web/docs` give every session the same context; `.claude/agents/` defines the `build-runner` and `wasm-porter` subagents.

### Serving the web app

The page must be cross-origin isolated for `SharedArrayBuffer`, so every response needs `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (or `credentialless`), and `.wasm` files need `application/wasm`. `localhost` counts as a secure context, so no TLS is needed locally.

- **Development:** the Vite dev server in `third_party/geospatial-web/app`, with both headers set in `server.headers` and the `install/wasm32` module aliased in. You get hot reload for JS; rebuilding the C++ means re-running `geo.py build wasm32`.
- **Production-like:** nginx in Podman or Docker serving the installed app with `tools/nginx.conf` (`geo.py serve docker`, see `tools/README.md`).

## Milestones

Two tracks ran in parallel and met at I1. **Status:** W0 to W2, C0 to C4 and I1 are done, as well as the additions D23 (grids) and D24 (ArcGIS 3D object layers); I2 is next, E1 is a follow-on. `NEXT_STEPS.md` has the details per milestone.

**Web track** (Maps SDK side, independent of the WASM port)

1. **W0 Spike.** A RenderNode with a minimal WebGL2 mesh pass draws a hard-coded mesh at a float32-exact origin, in a global and a local SceneView, served with COOP/COEP.
   - Exit: no jitter at street level; `crossOriginIsolated === true`; the SDK and basemaps load.
2. **W1 Renderer on recorded data.** The offline reference (option D) exports placed buffers and matrices to binary files, in the same layout the C3 observer will produce. The RenderNode loads them, so the web side is proven before the WASM build exists.
   - Exit: two placed 3DOLs from the offline export render correctly over the basemap.
3. **W2 Test harness.** Control points, Playwright harness and report page.
   - Exit: the harness scores W1's recorded data with pass, fail and unsupported results.

**C++ track** (proposal implementation and WASM port on geospatial-web)

4. **C0 Library skeleton.** usdGeospatial schema library with the proposal's current fields; PROJ found by CMake.
   - Exit: schemas register; the proposal's Eiffel Tower and UTM 11N examples load; ctest green.
5. **C1 Resolution and engine.** Binding resolution, validators, engine interface and plugin registry, usdGeospatialProj plugin, query API.
   - Exit: the UTM 11N and California zone 5 + NAVD88 examples resolve to the pyproj reference within 1 mm; broken bindings fail visibly; the engine is found through PlugRegistry, and a stub second engine can be selected with USDGEOSPATIAL\_ENGINE.
6. **C2 WASM port of Hydra and usdImaging.** Steps 1 to 7 of the WASM section above.
   - Exit: a local wasm32 build succeeds with `--usd-imaging` and the hd, hdsi and usdImaging ctests pass.
7. **C3 Geospatial scene index and observer.** Placement evaluation F(x) with orientation and time samples; filtering scene index; observer.
   - Exit: natively, two 3DOLs in different CRSs come out correctly placed in usdview and in the observer's dump.
8. **C4 Browser module.** usdGeospatial and the PROJ plugin in the WASM build (PROJ, SQLite and a trimmed proj.db built with emscripten), embind API, worker wrapper.
   - Exit: the C1 cases pass in a headless browser, with the same results as native PROJ. (Grid cases were unsupported in the browser until D23.)

**Integration**

9. **I1 End to end.** The web app swaps the recorded data for the live WASM module. The harness runs the whole test plan in the browser.
   - Exit: browser results match the native PROJ results within 1 cm on every case both engines can do.
10. **I2 Write-up.** Findings, numbers, and feedback to the proposal authors. Split the WASM imaging port into upstreamable PRs. Trim `proj.db` (D17) and measure download size.

**Follow-on**

11. **E1 Esri engine plugin.** usdGeospatialEsriPE behind the same interface, calling the Maps SDK's `projectOperator` from the worker through an emscripten JS bridge.
    - Exit: switching the engine needs no code change, and every case both engines can do agrees with PROJ within 1 cm.

## Test plan

The prototype passes when two 3DOLs in different spatial references land on their control points within tolerance, and every transform an engine cannot do (for example one needing a grid that is not served) is reported as unsupported instead of drawn in the wrong place.

**Method.** Each 3DOL carries 4 to 6 control points as named prims with known coordinates in an independent survey or reference CRS. After placement, the harness reads each point's render position back with `fromRenderCoordinates()`, projects the reference coordinates into the same SR, and reports the 3D error. Each case gets one of three results: **pass**, **fail** (over tolerance), or **unsupported** (detected before placement, with the reason).

**Proposed tolerances.** Client vs offline reference (option D): 1 cm, which tests the math. Client vs surveyed control points: 5 cm horizontal and 5 cm vertical, to be adjusted to the survey's stated accuracy.

| Case | Setup | Expected: browser (PROJ, grids on demand) | Expected: native (PROJ, pinned grids) |
| --- | --- | --- | --- |
| T1 Same datum | 3DOL A in a UTM zone (WGS84), 3DOL B in WGS84 geographic 3D | Pass | Pass |
| T2 Equation-based datum shift | 3DOL A in a national CRS with a Helmert transform to WGS84, 3DOL B in WGS84 | Pass; operation logged | Pass; operation logged |
| T3 Grid-based datum shift | NAD27 at Redlands: best operation NADCON (`us_noaa_conus.tif`, `us_noaa_cshpgn.tif`) | Pass: the worker fetches the grids on demand (D23) | Pass (pinned grids) |
| T4 Vertical CRS mismatch | The proposal's California zone 5 + NAVD88 compound example vs ellipsoidal heights | Pass: GEOID99 and NADCON5 grids fetched on demand (D23) | Pass (pinned grids) |
| T5 Matrix vs full point map | T1 and T2 data, placement matrix vs F(x) per vertex | Under 1 cm difference for 3DOLs up to 1 km | Same |
| T6 Precision | Global and local SceneView, camera 1 m from a control point | Under 1 px screen error | Not applicable |
| T7 Broken binding | Direct binding to a missing CRS prim, or missing crs:position | Error data source, nothing drawn, no fallback to the parent CRS | Same, plus validator report |
| T8 Orientation | 3DOL with heading 90, pitch and roll 0, and a time-sampled heading | Matches native result | Matches pyproj reference within 0.001 degrees |
| T9 Browser vs native | Every case both can place (EGM2008 is not served to the browser) | Under 1 mm from native | Reference |
| T10 WASM Hydra build | hd, hdsi and usdImaging ctests in the local wasm32 build | All pass | Not applicable |
| T11 Engine plugins | Select a stub engine, then (after E1) the Esri engine, by `USDGEOSPATIAL_ENGINE` | Results name the engine used; Esri matches PROJ within 1 cm where it can transform, else reports unsupported | Same with the stub engine |

The browser harness runs with Playwright and writes a JSON and HTML report per run. The native cases run as ctest in the usdGeospatial library, using the proposal's own examples and its Appendix C counterexamples as fixtures. A transform the engine cannot do counts as a pass when it is reported, which is what the proposal asks of an engine (requirement 21).

## Open questions

Settled during the work, with the decision that holds the answer: the RenderNode transform and render origin (D13),
the transform provider (D4, D5), how the target CRS is chosen (D13), pthreads and COOP/COEP (W0), the share of the
Autodesk diff needed (`WASM_GAP.md`), wasm32 vs wasm64 (D12), and binding by reference (proposal text, C1).

| Question | Proposed answer | How to settle it |
| --- | --- | --- |
| Can the Esri engine plugin call `projectOperator` synchronously from C++ in the worker? (E1) | The scene index needs synchronous answers. Load `projectOperator` inside the worker and call it through `EM_JS`. If the SDK won't load in a worker, pre-resolve all CRS pairs on the main thread before composing and pass the results in. | Spike at the start of E1. |
| Where does the C++ work go upstream? | Keep it on geospatial-web until the proposal is accepted. Offer the WASM imaging fixes upstream sooner, as separate PRs, since they are useful on their own. | Discuss with the proposal authors and Pixar at I2. |

## Risks

- **Proposal still moving.** Placement, orientation and chart conventions are under review. Fallback: pin to a PR commit, and keep field names in one tokens file so renames are cheap.
- **Static registration gaps.** Plugins silently missing in the static WASM link; this happened (usdGeospatialProj, the glTF plugin) and is handled by whole-archive linking. Any new plugin needs the same, plus a check that it registers.
- **PROJ in WASM adds size.** PROJ, SQLite and `proj.db` add several MB; `proj.db` is compressed now and trimmed at I2 (D17).
- **Download size.** OpenUSD with Hydra in WASM will be large. Acceptable for a prototype; record the number and try `-Oz` and wasm-opt in I2.
- **COEP breaks third-party assets.** The SDK's assets are self-hosted; ArcGIS services answer with CORS. A service without CORS would need a proxy or `credentialless`.

## Sources

- [Geospatial CRS proposal, PR #1](https://github.com/Tamrat-B/OpenUSD-proposals/pull/1)
- [OpenUSD v26.08 release](https://github.com/PixarAnimationStudios/OpenUSD/releases/tag/v26.08), its [WebAssembly build notes](https://github.com/PixarAnimationStudios/OpenUSD/blob/release/BUILDING.md) and the [geospatial-web branch](https://github.com/mistafunk/USD/tree/geospatial-web)
- [Autodesk USD fork](https://github.com/autodesk-forks/USD), branches `adsk/feature/wasm` and `adsk/hgiWebGPU`
- [RenderNode reference](https://developers.arcgis.com/javascript/latest/references/core/views/3d/webgl/RenderNode/) and [webgl utilities](https://developers.arcgis.com/javascript/latest/references/core/views/3d/webgl/)
- [Custom RenderNode windmills sample](https://developers.arcgis.com/javascript/latest/sample-code/custom-render-node-windmills)
- [projectOperator](https://developers.arcgis.com/javascript/latest/references/core/geometry/operators/projectOperator/) and [geographicTransformationUtils](https://developers.arcgis.com/javascript/latest/references/core/geometry/operators/support/geographicTransformationUtils/)
