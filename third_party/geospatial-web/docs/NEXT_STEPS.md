# Next steps for Claude Code

## State (2026-11-22)

Milestones W0 to W2, C0 to C4 and I1 are done (exit criteria in `PLAN.md`), plus three additions: grids for vertical
CRSs and grid-based datum shifts (D23), ArcGIS 3D object layers through wrapper layers with Adobe's glTF plugin (D24),
and the app's display geoid correction (D25). The app draws the live WASM module by default; the browser and native
results agree to 0.000 mm on every case both can place. How to build and test: `README.md` (development section) and
`tools/README.md`.

## Next

**I2 write-up and upstreaming**
- Done: `WRITEUP.md` (results, numbers, feedback for the proposal, findings).
- Split the WASM imaging port into upstreamable PRs (commits under `build_scripts/`, `cmake/`, `pxr/imaging`,
  `pxr/usdImaging/usdImaging`; see `WASM_GAP.md`).
- Done: `proj.db` is fetched at startup instead of embedded (D26): module 22.9 MB (5.8 MB gzip), `proj.db` 10.7 MB
  (2.0 MB gzip).
- Download size: `-Oz` at link time saves 3 % gzip, nothing with brotli. Brotli copies are installed and served by the
  dev server (module 3.8 MB, `proj.db` 1.3 MB). Still to measure: the `wasm32-minsize` preset (all of USD at `-Oz`).

**Open items**
- **3D object layer transform semantics are assumed:** scale, then rotate about an east/north/up axis (right-handed),
  then translate, in metres (from the SDK's MeshTransform; order and frame undocumented). The Zurich features are all
  identity. Verify on a layer with a rotated feature (Find lists candidates in the Zurich org).
- **Threads.** The module runs without a pre-started pthread pool and reports 24 work threads. With real models
  (the Zurich glb are 126 MB and 63 MB): measure, then pre-start a pool (`-sPTHREAD_POOL_SIZE`) and cap
  `setThreadLimit` to it.
- **Local views and heights.** The local view's output (`EPSG:3857` promoted to 3D) has ellipsoidal heights, which the
  geoid correction (D25) lowers for terrain; check whether a local scene needs its own vertical CRS choice.
- The anchor's own ordinary transform `A` (post-placement adjustment in the working chart) is still a candidate in
  the proposal; reference and C++ report it as unsupported.
- Native instancing under an anchor is untested (instances get separate prototypes; anchor paths into prototypes).
- Python wrappers for the C1 classes (binding, engine, registry, placement) do not exist yet.
- Control points are synthetic (1 cm, client vs reference); surveyed points (5 cm tolerance) need a real survey.
- `geo.py serve docker` mounts `install/wasm32/share/geospatial-web` as the site root, while the app loads the module
  from `/usd/`; install the built app next to it and map `/usd/` (the dev server already does).
- Hand-written files under `pxr/` use upstream's header ("Copyright 2026 Pixar"); change if Esri needs its own line.
- E1 (Esri engine plugin): see `PLAN.md`, including the open question about calling `projectOperator` synchronously.

## Milestone log

- **Tooling (2026-11-21):** `geo.py`, presets, pinned PROJ 9.9.0 and SQLite 3.54.1; WASM PROJ under Node matches
  native `cs2cs`. All non-upstream files live in `third_party/geospatial-web`.
- **W0:** RenderNode with float32-exact origins in global and local views, under COOP/COEP `require-corp`. Simon
  confirmed on a real GPU: origin mode steady, naive float32 jitters.
- **W1:** `reference/reference.py` writes the test scene and the recorded export (`RECORDED_FORMAT.md`); the app draws
  it. Mast on Site A's corner: 1.3e-7 render units (global), 5e-5 Web Mercator units (local).
- **W2:** control points and test cases (`geotest:` attributes), harness and reports (`app/reports/`). Both views as
  expected; worst passing error 0.1 mm.
- **C0:** `pxr/usd/usdGeospatial` schemas (`GeospatialCRS`, `GeospatialCRSBindingAPI`) from `schema.usda`; the
  proposal's examples load.
- **C1:** binding resolution and validation, engine interface, registry (API > `USDGEOSPATIAL_ENGINE` > plugInfo
  priority), placement query; `usdGeospatialProj` (priority 100). UTM 11N, California zone 5 ftUS and the Eiffel Tower
  match pyproj within 1 mm (0 mm measured).
- **C2:** GPU-free Hydra and usdImaging for wasm32 with few changes (`WASM_GAP.md`), plus upstream bugs Clang on wasm32
  exposed (stack-frame return, TetMesh face hash shift, printf formats, crate float compression). hd and hdsi tests pass;
  `testUsdImagingInstanceProxyTraversal_1/_2` excluded (32-bit hash suffixes).
- **C3:** `pxr/usdImaging/usdGeospatialImaging` (D16): binding adapter, placement scene index (`geospatialPlacement`,
  optional `applyToXform`), scene index plugin for usdview (`geo.py usdview`), mesh observer; `geospatialRecord` dump
  (`geo.py record`). Reproduces the reference export; Simon confirmed the placed scene in usdview.
- **C4:** `module/geospatialModule` (embind, worker, `c4.html`): C1 cases at 0.000 mm vs pyproj in Chromium and Node,
  live scene chain matches the export (matrices within 4e-8). Init 158 ms, first placement 54 ms.
- **I1:** live module in the app by default (D21), exact mode (`&exact=1`), sites F to K for T2 to T8 (D20). T9 browser
  vs native 0.000 mm; T5 under 0.15 mm except the 1 km runway in Web Mercator (25 mm, matrix shortcut); T6 0.001 px at
  1 m; T11 every result names `proj`.
- **ArcGIS 3D object layers (D24):** wrapper layer per layer (one `GeospatialCRS`, per feature an anchor and a model
  prim referencing the glb), Adobe's glTF plugin in `third_party/adobe-usd-gltf`, mocks in `testdata/arcgis`.
- **D23 grids (2026-11-22):** libtiff and PROJ with TIFF for both targets, six pinned grids (EGM2008 native only),
  `missingGrids` through engine, placement, schema, observer and module, `ResetEngines()`, browser fetches grids on
  demand; 2D CRSs get the EGM2008 default height. Sites D and H pass, G uses NADCON; T9 0.000 mm on 9 anchors including
  G and H; the EGM96 mock layer is placed at 474.56 m ellipsoidal, as native `cs2cs`.
- **App diagnostics and D25 (2026-11-22):** load status and log, one layer combobox (URL, item id, item page, mocks)
  with Find (public 3D object layers of an org, default zurich.maps.arcgis.com, kept in local storage), World
  Elevation terrain and Open 3D Buildings as references, display geoid correction (module `geoidHeights`). The live
  Zurich office layer loads (14 loadable layers in the org).

## Findings

**Coordinate operations (PROJ, engine, reference)**
- PROJ offers "Ballpark" operations (accuracy unknown) when nothing better is usable; the engine disables them
  (`proj_operation_factory_context_set_allow_ballpark`) so cases report unsupported instead of placing metres off.
- **Operations must be chosen for the anchor's location.** Without an area of interest PROJ ranks operations for the
  whole datum (NAD27 at Redlands got a Canadian operation, 12 m off); the engine takes a `UsdGeospatialAreaOfInterest`,
  and the reference uses the same area.
- **Operation choice depends on the grid policy and the grids present.** The engine discards operations needing
  missing grids and superseded ones, and the reference uses the same policy (`best_transformer`). With the pinned grids
  the C1 California zone 5 case uses HPGN (`NAD83 to WGS 84 (54)`).
- **Projected outputs go through their geographic 3D CRS.** PROJ's direct operation into a projected CRS with an
  ellipsoidal height axis wraps the datum shift in `push/pop v_3` and keeps the source height (21 m off for NAD27).
- Grid-aware comparisons: the reference records `gridsUsed`; a result is "not comparable" only if it reports one of
  those missing. A missing grid of a better, unused operation does not count (EGM2008's 1' grid is not on the PROJ CDN).
- PROJ reports accuracy -1 for conversions; the engine reports 0 when every step is a conversion.
- pyproj's `geodetic_crs` of a derived Cartesian CRS is the CRS itself; the reference uses its base CRS.
- WASM matrices differ from native by up to 4e-8 (native vs pyproj: 0 to 1e-9), most likely the C library's
  trigonometry; well within tolerance.

**Maps SDK**
- **Global render space is not EPSG:4978** but ECEF on a 6378137 m sphere (D13). `renderCoordinateTransformAt` is
  metric on that sphere and drifts from lon/lat content (2.4 cm over 22 m); the app uses the derivative of
  `toRenderCoordinates` instead.
- **Local Web Mercator scenes are not metric:** 1 m = 1 Mercator unit, so placement includes the projection scale.
  EPSG:3857 is not exactly conformal on the ellipsoid (34 deg N: scale 1.2072 vs 1.2099, 0.11 deg angle change);
  real local scenes are better in a conformal CRS with scale near 1 (UTM, LV95).
- **The matrix shortcut is not enough at 1 km in Web Mercator** (25 mm at the runway ends; under 1 um in the global
  ENU frame). Options for real data: exact mode for large models, or one matrix per tile.
- The SceneView draws all heights as ellipsoidal; World Elevation's are orthometric with no geoid named (its service
  description). Hence D25.
- The SDK reports Web Mercator as wkid 102100 (latestWkid 3857); compare both.
- The CDN worker is blocked under COEP `require-corp`: the SDK's assets are self-hosted.
- The SDK does not know RenderNode geometry when choosing clip distances; close-range views set
  `view.constraints.clipDistance`.

**ArcGIS 3D object layers**
- `query3D` returns asset maps only with `returnGeometry=true`.
- Layers without the `Query` capability (e.g. Urban model layers, `ChangeTracking` only) reject `query3D`; the app
  says so before querying.
- Models are glb (USD is an upload format, not a query format); CityEngine exports are Y-up, metres, x east, y up,
  z south, centred on the origin. Asset downloads on ArcGIS Online send `access-control-allow-origin: *`.
- Without `vcsWkid` the wrapper takes z as ellipsoidal height (promoted 3D CRS); the layer does not say.

**WebAssembly build**
- emscripten 5.0.7 needs Python >= 3.10; the venv is Python 3.12 and `geo.py` sets `EMSDK_PYTHON`.
- USD's WASM build uses `-fexceptions`; PROJ and SQLite must too, or PROJ's internal throws abort.
- **Static builds must link plugins whole-archive.** Only linked libraries have their `plugInfo.json` embedded under
  `/usd`, and USD whole-archives only its own libraries: the PROJ engine and the glTF plugin lost their registrations
  until linked whole-archive. Any new plugin needs the same.
- PROJ's `USE_ONLY_EMBEDDED_RESOURCE_FILES` compiles out all file access, so grids could never load; WASM PROJ embeds
  `proj.db` without it. Since D26 nothing is embedded: WASM tests mount `share/proj` under Node, the browser fetches it.
- libtiff 4.7's CMake config links `ZLIB::ZLIB` without finding zlib, and its configure picks up a broken system
  libjpeg-turbo package even with JPEG off: `geo.py` disables codec lookups and injects `tools/cmake/zlib-target.cmake`.
- **Env settings are read when a library loads**, before `main`, so a program cannot set `USDGEOSPATIALIMAGING_*` for
  itself; WASM processes do not see the test runner's environment either. The module appends the placement scene index
  itself.
- `pxr_cpp_bin` installs only the `.js` of an Emscripten program, not its `.wasm`. Emscripten programs see host files
  only through NODEFS mounts.

**Hydra and usdImaging**
- usdImaging plugin scene indices run after flattening: they see world transforms and flattened data sources; the
  anchor's own transform check reads the stage (`hasOwnTransform`).
- Flag time variability on every `Get`: a data source container can outlive the edit that makes an attribute
  time-varying, so flags set only in a constructor are missed.
- usdview's bounding box, "Local to World Xform" and Vis column come from UsdGeom on the stage, so they show the
  unplaced scene while the viewport is placed.

**Tooling and tests**
- `usdGenSchema` derives names from the class and properties: use `fileName` and `apiName` in `schema.usda`.
  Regenerate with the native install: `PYTHONPATH=<ws>/install/native-debug/lib/python3.12/site-packages
  <ws>/venv/bin/python3 <ws>/install/native-debug/bin/usdGenSchema schema.usda .` in `pxr/usd/usdGeospatial`; `api.h`
  must be in `PUBLIC_HEADERS`. Hydra schemas: `hdGenSchema.py --all` in `pxr/usdImaging/usdGeospatialImaging`.
- The presets put the venv on PATH, so CLion and plain `cmake --preset` find `pyside6-uic` and emcc finds Python.
- Playwright uses the GPU by default (all specs in under a minute); `GEO_SOFTWARE_GL=1` for software WebGL. The SDK
  cancels tile requests when the camera jumps; specs ignore `ERR_ABORTED` on tile URLs only.
