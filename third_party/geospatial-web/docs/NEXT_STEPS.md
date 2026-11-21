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
and displaced by more than a metre at 1 m. **W0 done:** Simon confirmed on a real GPU that origin mode is steady while
the camera moves and naive mode jitters. Both `bootstrap native` and `bootstrap wasm32` (including `usd-deps`) completed.

**Update 2026-11-21, open questions:** all settled except the E1 Esri-engine question and where the C++ goes upstream
(I2). New: D14 (`GeospatialCRS`). Autodesk diff sized in `WASM_GAP.md`; the GPU-free subset gains hdar and hio.
Next: C0 (`usdGeospatial` schema library) and C2 (WASM port) in parallel.

**Update 2026-11-21, W1 (Claude Code):** `reference/reference.py` (usd-core 26.8, pyproj 3.8.0 with PROJ 9.8.1, numpy;
venv `<workspace>/venv-reference`, Python >= 3.12) writes the W1 fixtures (`testdata/w1/`, now `testdata/scene/`: CRS library with
`GeospatialCRS` prims, Site A building + wing in 3D UTM 11N with heading 30, Site B mast in EPSG:4979 on Site A's
corner via PROJ's topocentric conversion) and exports recorded data per `docs/RECORDED_FORMAT.md` to
`app/public/recorded/`. `reference -- check`: mast on corner and linearization error both below 1 um.
The app draws the export by default (`?data=recorded`; W0 meshes under `?data=w0`). Playwright `w1 global|local`
passes: both anchors ok, mast-to-corner distance in render space 1.3e-7 (global) and 5e-5 Web Mercator units (local).
Exit criterion met pending Simon's visual check (`geo.py serve dev`, presets Overview / Street level / Corner).

**Update 2026-11-21, W2 (Claude Code):** control points and test cases in the scene (`testdata/scene`, `geotest:`
attributes; see `reference/README.md`): Sites A and B pass, C has a planted 5 cm error and must fail, D (2D CRS) and
E (missing CRS prim) must be reported as unsupported. `reference -- check` scores them offline with the exact point map;
the app's harness (`src/harness.ts`, Score button) scores them in the browser through `fromRenderCoordinates`, and
Playwright `w2 global|local` writes `app/reports/w2-<time>.{json,html}`. Both views: 2 pass, 1 fail, 2 unsupported,
0 unexpected; worst passing error 0.1 mm (local Web Mercator view, matrix linearization). **W2 exit met.**
Next: C0 and C2 (C++ track); the web track waits for C3/C4.

**Update 2026-11-21, C0 (Claude Code):** `pxr/usd/usdGeospatial` with `GeospatialCRS` (`UsdGeospatialCRS`,
`uniform token crs:wkt`) and `GeospatialCRSBindingAPI` (`UsdGeospatialCRSBindingAPI`, Xformable only,
`double3 crs:position` without fallback, `double3 crs:orientation` = (0, 0, 0)), generated with `usdGenSchema` from
`schema.usda`. Tests `testUsdGeospatialSchemas` (Python) and `testUsdGeospatialSchemasCpp` (C++, for WASM) load the
proposal's CRS examples verbatim, the Eiffel Tower example and a UTM 11N site with a nested and a broken binding.
`PXR_BUILD_GEOSPATIAL_PROJ` (default OFF, ON in our presets) finds PROJ 9.9.0. Native debug build and
`geo.py test native-debug -- -R UsdGeospatial` pass. **C0 exit met.** Next: C1 (binding resolution, engine interface and
registry, usdGeospatialProj) and C2 (WASM port).

**Update 2026-11-21, C1 (Claude Code):** in `pxr/usd/usdGeospatial`: binding resolution and placement validation
(`binding.h`), the engine interface (`engine.h`: `DescribeCrs`, `CanTransform`, `Transform` in semantic tuple order;
results name engine, operation, accuracy), the registry (`engineRegistry.h`: PlugRegistry discovery, `engineName` and
`priority` in plugInfo, selection by API > `USDGEOSPATIAL_ENGINE` > priority) and the placement query (`placement.h`:
the proposal's point map per anchor, origin and drawing matrix in the output CRS, exact per-point map, slerped
time-sampled orientation). The PROJ engine is `pxr/usd/plugin/usdGeospatialProj` (priority 100). Tests: resolution,
registry (test-only stub engine selected with `USDGEOSPATIAL_ENGINE=stub`), and PROJ placement against pyproj
(`reference.py proj-expected`): UTM 11N 3D, NAD83 California zone 5 ftUS 3D and the proposal's Eiffel Tower match
within 1 mm (0 mm measured); the proposal's 2D UTM 11N and California zone 5 + NAVD88 are reported unsupported.
All 5 `UsdGeospatial` ctests pass. **C1 met, with the grid question open** (see findings). Not done yet: Python
wrappers for the new classes, UsdValidator registration of the validation checks, the anchor's own transform `A`,
2D CRSs with the EGM2008 default height.

**Update 2026-11-21, C2 (Claude Code):** GPU-free Hydra and usdImaging build for wasm32 with few changes:
`build_usd.py` allows `--imaging`/`--usd-imaging` for wasm (GL off, OpenSubdiv `NO_OPENGL`, `BUILD_SHARED_LIBS` typo),
`hgi` has no default backend on WASM, `testUsdImagingMaterialBinding` drops an unused `garch` link, `testHdTiny` is
only built with `hdx`, plus missing `override` markers and the Emscripten test-runner message printed once. Clang on
wasm32 also showed real upstream bugs, fixed separately (stack-frame return, 32-bit shift in the TetMesh face hash,
64-bit printf formats, out-of-range float-to-int in crate float compression). WASM test run
(`geo.py test wasm32 -- -R "testHd|testUsdImaging|UsdGeospatial"`): 34 of 37 passed; all hd and hdsi tests pass.
`testUsdImagingInstanceProxyTraversal_1/_2` differ only in 32-bit hash suffixes (documented in `WASM_GAP.md`,
excluded in the `wasm32` test preset); `testUsdGeospatialProjPlacement` failed because the static build did not link
the PROJ engine plugin, and static plugins were not whole-archived so their registrations were dropped (both fixed, see
`WASM_GAP.md`); it now passes under Node with the same results as native (0 mm vs pyproj), an early T9 data point.

**Update 2026-11-21, C3 (Claude Code):** new library `pxr/usdImaging/usdGeospatialImaging` (D16):
- `GeospatialCRSBindingAPI` adapter: `geospatialBinding` (anchor path; flattened to descendants through the scene
  index plugin's flattened data source provider) and `geospatialAnchor` (the anchor's placement inputs at the current
  time, read with C1's `UsdGeospatialComputePlacementInputs`, so Hydra and the stage query agree by construction).
- `UsdGeospatialImagingPlacementSceneIndex`: places each anchor once (cached, invalidated by edits, time changes and
  transform changes), adds `geospatialPlacement` (status, reason, engine, operation, accuracy, source CRS, origin,
  frame, `modelToFrame`, optional `linearizationError`) to every bound prim, hides prims that cannot be placed, and
  with `applyToXform` rewrites transforms into a shared metric display frame (ENU at a display origin, or output CRS
  units), mapped back to the stage's units and up axis.
- Scene index plugin: `USDGEOSPATIALIMAGING_OUTPUT_CRS` (and `_DISPLAY_ORIGIN`) turns the placement on in any
  usdImaging client, e.g. usdview; off by default.
- `UsdGeospatialImagingMeshObserver`: flat-shaded triangles, colour, matrix and status per mesh, placement per anchor.
- `usdGeospatial` changes: value-based `Resolve(UsdGeospatialPlacementInputs)`, `GetSourceCrs()`,
  `UsdGeospatialComputeEnuToEcef`, and 2D output CRSs promoted to 3D (`UsdGeospatialCrsInfo::promoted3D`, PROJ
  `proj_crs_promote_to_3D`), as pyproj's `to_3d()` does in the reference.
- `third_party/geospatial-web/module/geospatialRecord` (`PXR_BUILD_GEOSPATIAL_WEB`, root `CMakeLists.txt` hook):
  writes the observer's view in the recorded format; `geo.py record`, app `?src=recorded-cpp`.

Tests: `testUsdGeospatialImagingPlacement` reproduces the reference export of the W1/W2 scene in both outputs (every
status, origin and matrix, buffers, linearization errors) and checks that Site B's mast (EPSG:4979) stands on Site A's
building corner (UTM 11N) in the display frame (2.6e-8 m global), that a heading edit and a time-sampled heading
(slerp) re-place the scene like the stage query; `testUsdGeospatialImagingPlugin` checks what usdview sees (native
only). All 7 `UsdGeospatial` ctests pass natively. `geo.py record` + `reference -- compare`: 0.000 mm against pyproj;
Playwright W1/W2 on the C++ dump (`GEO_RECORDED_SRC=recorded-cpp`) pass with the same scores as the reference.
**C3 exit met natively:** Simon confirmed the placed scene in usdview (`USDGEOSPATIALIMAGING_OUTPUT_CRS=EPSG:4979`; now `geo.py usdview`). WASM build of the new library not run yet.

**Update 2026-11-21, C4 (Claude Code):** `module/geospatialModule` (wasm32 only): an ES6 embind module
(`createGeospatialModule`) with usdImaging, `usdGeospatialImaging` and the PROJ engine linked whole-archive.
`GeospatialScene`: `open`, `setOutput(crs, measure)` (builds the usdImaging chain plus the placement scene index and
observer; 2D outputs promoted), `setTime`, `sync`, `anchors`, `meshes` (typed-array views, copied by the worker),
`resolve(anchor, crs, points)` (C1 query, exact points); `info()`, `setThreadLimit(n)`. Installed with the C1 and scene
fixtures to `install/wasm32/share/geospatial-web`, served by Vite at `/usd`. App side: `src/usd/worker.ts` (owns
module and stage, fetches files into MEMFS, transfers buffers), `src/usd/client.ts`, check page `c4.html`.
Playwright `c4` (headless Chromium, GPU not needed): the five C1 cases match pyproj at 0.000 mm (origins and model
points) with the grid cases reported unsupported, and the live scene chain matches the reference export in both
outputs (matrices within 4e-8). Under Node the same module gives the same C1 results. Init 158 ms, first placement
of the test scene 54 ms. **C4 exit met.** Size: `geospatialModule.wasm` 30.7 MB (Release), of which the embedded
`proj.db` is 10.7 MB; served precompressed, 7.2 MB with gzip (4.6 MB with brotli); proj.db trimmed at I2 (D17).

**Update 2026-11-21, I1 (Claude Code):** the app draws the live module by default (D21): `src/live.ts` loads the
test scene through the worker, evaluates it at `geotest:time`, places it in EPSG:4979 (global) or the view's spatial
reference (local, promoted to 3D) and scores it with the harness; `&exact=1` draws exact per-vertex points
(measurement mode: placement scene index `exactPoints`, `framePoints`). New test sites (reference `fixtures`): F (T2,
WGS 72 Helmert), G (T3, NAD27 without grids, D22), H (T4, NAVD88), I (T5, 1 km runway), J and K (T8, heading 90 and a
slerped time-sampled orientation). pyproj is built on our PROJ 9.9.0 (D19). Engine: `skippedOperations` (D22) and an
area of interest. Playwright `i1` (global and local, matrix and exact; report `app/reports/i1-*`): all 11 cases as
expected in every run; T9 browser vs native (`geo.py record`) 0.000 mm on every placed anchor, matrices within 4e-8,
same against the reference; T5 under 0.15 mm except the runway in Web Mercator (25 mm, matrix shortcut; exact mode
passes); T6 0.001 px at 1 m in both views; T11 every result names `proj`. Native: 7 `UsdGeospatial` ctests pass,
`reference -- compare` 62 checks ok. **I1 exit met** (browser vs native within 1 cm on every case both can do;
measured 0 mm). WASM ctest run of the changed C++ pending (Simon).

**Update 2026-11-21, ArcGIS 3D object layers (Claude Code):** vanilla models of 3D object layers are placed through a
generated wrapper layer (D24, D23 pending). Read from a real layer (Zurich office 4th floor, item a457bef2..., with
Simon's go-ahead): the layer's `spatialReference` (`wkid` 4326, `vcsWkid` 5773 EGM96) and per feature the
`infoFor3D.transformFieldRoles` fields (`ESRI3DO_OX/OY/OZ` origin in the layer SR, translation, scale, rotation axis
and degrees); `query3D` gives the asset maps; models are glb only (USD is an upload format, not a query format),
CityEngine exports: glTF Y-up, metres, x east, y up, z south, centred on the origin. Done:
- `third_party/adobe-usd-gltf`: Adobe's glTF plugin from the 3dtiles prototype, ported to v26.08, native and WASM
  (tinygltf pinned in `geo.py`, OpenImageIO optional, warnings of the vendored code silenced, two format bugs fixed).
- Module: `crsWkt(definition, promote3D)`; worker: `fetchFiles`, `writeText`, `open`, `crsWkt`.
- App: `?layer=<url>` (REST or mock) builds the wrapper (`src/arcgis/objectLayer.ts`): one `GeospatialCRS` (compound
  with the vertical CRS, else promoted to 3D: z taken as ellipsoidal), per feature an anchor at the origin and a child
  prim with the feature's transform (in Y-up axes) referencing the glb.
- Mocks (`reference.py arcgis-mock`, `testdata/arcgis`) and `tests/objectLayer.spec.ts`: WGS 84 mock placed at the
  origins in both views; EGM96 mock reported unsupported, naming the missing grid; a rotated, scaled, translated
  model matches R(S p) + T. All 17 browser specs pass.

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
- (W1) **`renderCoordinateTransformAt` is metric on the SDK sphere.** Its frame has unit-length axes, but the SDK places
  lon/lat content on a 6378137 m sphere, 0.1 % (east-west) to 0.3 % (north-south, latitude dependent) off the WGS84
  ellipsoid. Meshes drawn in that frame drift from lon/lat-placed content: 2.4 cm over 22 m in W1, about 1 m at the
  edge of a 1 km model. The app uses the derivative of the SDK's own mapping (`toRenderCoordinates`) instead.
- (W1) The SDK reports Web Mercator as wkid 102100 (latestWkid 3857); compare both.
- (W1) EPSG:3857 is not exactly conformal on the ellipsoid: at 34 deg N the local scale is 1.2072 along one model
  axis and 1.2099 along the other, with a 0.11 deg angle change. The reference keeps this, so local views are true.
- (W1, T9) pyproj 3.8.0 bundles PROJ 9.8.1; the native and WASM builds use PROJ 9.9.0. Decide before I1 whether the
  reference should build pyproj against our PROJ.
- (W1, C3) The anchor's own ordinary transform `A` (post-placement adjustment in the working chart) is still a candidate
  in the proposal; the reference reports it as unsupported. W1 fixtures author none.
- (W2) Control points use test-only `geotest:` attributes, not proposal schema. Surveyed control points (5 cm tolerance)
  still need a real survey; the current ones are synthetic (1 cm, client vs reference).
- (W2) Playwright: the SDK cancels tile requests when the camera jumps; specs ignore `ERR_ABORTED` on tile URLs only.
- (W2) Playwright ran WebGL in software (SwiftShader) everywhere, which made the specs slow (2 min 49 s, W0 global
  55 s). The GPU is now the default: 30 s for all specs on an RTX 2080; `GEO_SOFTWARE_GL=1` keeps the software path.
  Playwright's Ubuntu 24.04 fallback build on AlmaLinux is not the issue: it uses the GPU as fast as system Chrome.
- (C0) `usdGenSchema` derives file and accessor names from the class name and property names: use `fileName`
  (`crs.h`, not `cRS.h`) and `apiName` (`GetPositionAttr`, not `GetCrsPositionAttr`) in `schema.usda`. Regenerate with
  the project's native install (same Python as the venv): `PYTHONPATH=<ws>/install/native-debug/lib/python3.12/site-packages
  <ws>/venv/bin/python3 <ws>/install/native-debug/bin/usdGenSchema schema.usda .` (in `pxr/usd/usdGeospatial`). `api.h` must be listed
  under `PUBLIC_HEADERS` in `CMakeLists.txt`.
- (C0) The presets put the venv on PATH (bin and Scripts), so CLion and plain `cmake --preset` find `pyside6-uic`.
- (C0) Hand-written files under `pxr/` use upstream's header ("Copyright 2026 Pixar"), assumed for upstreamable code;
  change if Esri needs its own line.
- (C1, open, Simon: "decide later") **Grids natively.** Native PROJ has no libtiff, so it reads no geoid or datum
  grids: the proposal's 2D UTM 11N (EGM2008 default height) and California zone 5 + NAVD88 examples are reported
  unsupported natively too, and T3/T4 cannot pass natively. Enabling them means libtiff in native PROJ plus pinned grid
  downloads (EGM2008, NAVD88 geoid) shared with pyproj. Decide before I1.
- (C1) **Operation choice depends on the grid policy.** With operations needing missing grids discarded while listing
  (our engine), PROJ ranks the ESRI chain via `WGS_1984_(ITRF00)_To_NAD_1983` (0.1 m) first for NAD83 -> WGS 84; with
  pyproj's default it ranks the EPSG null transform "NAD83 to WGS 84 (1)" (4 m) first, 1.2 m apart. The reference now
  uses the engine's policy (`best_transformer`), and the engine also discards superseded operations like pyproj.
- (C1) PROJ reports accuracy -1 for conversions (projections, axis changes); the engine reports 0 when every step of
  an operation is a conversion.
- (C1) pyproj's `geodetic_crs` of a derived Cartesian CRS (the Eiffel Tower tangent plane) is the CRS itself; the
  reference now uses its base CRS (`source_geographic_3d`), as the C++ engine does.
- (C1, C4) The engine registry relies on PlugRegistry types and loading plugins; for the static WASM build the PROJ
  engine must be registered at static-init time (PLAN, WASM section). Not tested yet.
- (C2) The project venv moved from Python 3.9 to 3.12 (`VENV_PYTHON` in `geo.py`), so emcc finds a Python >= 3.10 on
  PATH when CLion configures `wasm32`. The native build must be reconfigured (`geo.py build native-debug --fresh`): its
  Python bindings are rebuilt for 3.12. `install/upstream-native` (the build_usd.py reference) still has Python 3.9
  bindings; only rebuild it (`bootstrap native --steps usd-deps`) if its Python modules are needed.
  `venv-reference` uses the same Python 3.12 (it was 3.13) and is recreated automatically on a mismatch; its
  outputs (fixtures, recorded data, PROJ expectations) are identical after the switch.
- (C2, C4) **Static builds must link plugins.** In the WASM build a plugin is a static library; only linked libraries
  have their `plugInfo.json` embedded under `/usd`, so the engine registry only sees engines linked into the program.
  The C4 browser module must link `usdGeospatialProj` (and any scene index plugins) explicitly.
- (C3) **usdImaging plugin scene indices run after flattening.** They see world transforms and flattened data
  sources (provided by `FlattenedDataSourceProviders`), not local transforms; the anchor's own transform check reads
  the stage instead (`hasOwnTransform`).
- (C3) **Flag time variability on every `Get`.** A data source container can outlive the edit that makes an attribute
  time-varying (the flattening scene index keeps the input container), so flags set only in a constructor are missed;
  upstream's attribute data sources are created per `Get`, which is why they work.
- (C3, C4) **Env settings are read when a library loads.** `TF_DEFINE_ENV_SETTING` registers a registry function that
  reads the setting once Tf's env settings are first used, before `main` for linked libraries, so a program cannot set
  `USDGEOSPATIALIMAGING_*` for itself. The browser module must append the placement scene index itself (output CRS
  from the app at runtime), not rely on the plugin's env settings. WASM processes do not see the test runner's
  environment either, so `testUsdGeospatialImagingPlugin` is native only.
- (C3, C4) The local view's output (`EPSG:3857`, 2D) is promoted to 3D with ellipsoidal heights; the SDK's local
  scenes treat z as elevation. Check at I1 whether that needs a vertical CRS choice.
- (C3) Native instancing under an anchor is not handled (instances get separate prototypes through
  `InstanceDataSourceNames`, but anchor paths into prototypes are untested).
- (C3) usdview's bounding box, "Local to World Xform" and the Vis column come from UsdGeom on the stage, not from
  Hydra, so they show the unplaced scene (identity transforms, Sites D and E visible) while the viewport is placed.
  Expected with a scene index that only changes Hydra data.
- (C4) **Threads.** The module runs without a pre-started pthread pool and reports 24 work threads (the machine's
  cores); placement of small scenes is fast enough that whether TBB's threads ever start does not show. Before I1
  with real models: measure, then pre-start a pool (`-sPTHREAD_POOL_SIZE`) and cap `setThreadLimit` to it.
- (C4, I2) **proj.db is untrimmed** (10.7 MB raw, a third of the module). Decided (D17): serve the module
  precompressed now (`geospatialModule.wasm.gz`, 7.2 MB, written at install; nginx `gzip_static`, dev server too),
  trim proj.db at I2. Trimming means a reduced database that keeps every referenced datum, ellipsoid, operation and
  extent consistent.
- (C4, I1) `geo.py serve docker` mounts `install/wasm32/share/geospatial-web` as the site root, while the app loads
  the module from `/usd/`; I1 installs the built app next to it and maps `/usd/` (dev server already does).
- (C4) `pxr_cpp_bin` installs only the `.js` of an Emscripten program, not its `.wasm` (upstream; seen with
  `geospatialRecord`, now native-only). Emscripten programs only see host files through NODEFS mounts, as upstream's
  `test.pre.js` does for tests.
- (C4) WASM matrices differ from native by up to 4e-8 (native vs pyproj: 0 to 1e-9), most likely the C library's
  trigonometry; well within tolerance.
- (I1) **Operations must be chosen for the anchor's location.** Without an area of interest PROJ ranks operations for
  the whole datum: NAD27 at Redlands got "NAD27 to WGS 84 (3)" (Canada, 20 m) instead of "(6)" (CONUS west, 7 m),
  12 m apart. The engine interface now takes a `UsdGeospatialAreaOfInterest`; placement passes the anchor. The C1
  California case changed operation too ("NAD83 to WGS 84 (1)" instead of the ITRF00 chain). The reference had the
  same blind spot, so tests agreed; it now uses the same area.
- (I1) **Projected outputs go through their geographic 3D CRS.** PROJ's direct operation into a projected CRS with an
  ellipsoidal height axis (e.g. promoted Web Mercator) wraps the datum shift in `push/pop v_3`, keeping the source
  height: 2 m off for WGS 72, 21 m for NAD27 at Redlands. Placement now shifts datum into the output's geographic 3D
  CRS, then projects (a conversion). Found by the local W2 run; native and reference had agreed on the wrong result.
- (I1, T5) **The matrix shortcut is not enough at 1 km in Web Mercator:** 25 mm at the runway ends (Mercator's scale
  changes across the model); under 1 um in the global view's ENU frame. Exact mode (`&exact=1`) places per vertex.
  Options for real data: exact mode for large models, or one matrix per tile.
- (I1, T6) The float32 pipeline puts a point 1 m from the camera within 0.001 px of a Float64 projection (CPU
  emulation of the shader's float32 arithmetic), in both views.
- (I1, D22) Skipped grid operations are now specific to the site: NAD27 at Redlands skips "NAD27 to NAD83 (1) + NAD83
  to WGS 84 (54)" (NADCON5 CONUS, 2.15 m).
- (3DOL) **Transform semantics are assumed:** scale, then rotate about an east/north/up axis (right-handed), then
  translate, in metres (from the SDK's MeshTransform; order and frame undocumented). The Zurich features are all
  identity. Verify on a layer with a rotated feature before relying on it.
- (3DOL) Without `vcsWkid` the wrapper takes z as ellipsoidal height (promoted 3D CRS); the layer does not say.
- (3DOL) Next: D23 (grids: libtiff in PROJ natively and for WASM, pinned EGM96/NADCON5/NAVD88 geoid, on-demand fetch
  in the browser), then the live Zurich layer at its correct height. Its glb are 126 MB and 63 MB.
- (3DOL, glTF) USD whole-archives only its own libraries; a plugin built outside `pxr/` lost its file format
  registration in the static WASM link until the module whole-archived it (same class as the C2 plugin fix).
