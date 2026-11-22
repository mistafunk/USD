# OpenUSD geospatial CRS support on the web: prototype write-up (I2)

2026-11-22 · Simon Haegler

## Summary

The prototype implements the [geospatial CRS proposal](https://github.com/Tamrat-B/OpenUSD-proposals/pull/1) on
OpenUSD v26.08 in C++, runs it natively and in the browser (WebAssembly, with Hydra and usdImaging), and draws the
placed scene on the ArcGIS Maps SDK for JavaScript. Models in different coordinate reference systems land on their
control points: the browser and native results agree to 0.000 mm, both match an independent Python reference (usd-core
and pyproj), and every placement an engine cannot do is reported instead of drawn.

What was built (details in `PLAN.md` and `DECISIONS.md`):

- `usdGeospatial`: the `GeospatialCRS` and `GeospatialCRSBindingAPI` schemas, binding resolution, the placement F(x)
  and a placement query, and a coordinate operation engine interface with a plugin registry. No PROJ dependency.
- `usdGeospatialProj`: the PROJ engine plugin, native and WebAssembly, with grids for vertical CRSs and grid-based
  datum shifts.
- `usdGeospatialImaging`: a usdImaging adapter for the binding, a placement scene index that resolves every anchor into
  the consumer's output CRS in double precision, and a scene index observer that feeds the browser renderer.
- A WebAssembly build of the GPU-free part of Hydra and usdImaging on v26.08.
- A browser module (embind) in a Web Worker, and a Maps SDK app that draws the observer's meshes in a `RenderNode`.
- ArcGIS 3D object layers: their glb models placed through a generated USD wrapper layer, without changing the models.

## Results

Test plan (`PLAN.md`), measured in the browser (PROJ in WebAssembly) and natively:

| Case | Result |
| --- | --- |
| T1 Same datum, T2 Helmert datum shift | Pass, the operation used is named |
| T3 Grid-based datum shift (NAD27) | Pass with NADCON (accuracy 2.15 m); the browser fetches the grids it needs |
| T4 Vertical CRS (California zone 5 + NAVD88) | Pass with GEOID99 and NADCON5 grids |
| 2D CRS (the proposal's EGM2008 default height) | Pass natively; unsupported in the browser, which does not serve the 77 MB EGM2008 grid |
| T5 Placement matrix vs per-vertex F(x) | Under 0.15 mm, except 25 mm at the ends of a 1 km model in Web Mercator (exact mode places per vertex) |
| T6 Precision at 1 m from the camera | 0.001 px, global and local views |
| T7 Broken binding | Reported, not drawn, no fallback to the parent CRS |
| T8 Orientation (heading 90, time-sampled heading) | Pass, matches the reference |
| T9 Browser vs native | 0.000 mm on every anchor both can place; matrices within 4e-8 |
| T10 WebAssembly Hydra build | hd, hdsi, usdImaging and usdGeospatial tests pass under Node (two instance-proxy tests excluded: 32-bit hash suffixes) |
| T11 Engine plugins | Every result names its engine; a stub engine is selected through `USDGEOSPATIAL_ENGINE` |

Control points are synthetic: the tolerance is 1 cm against the reference, not a survey.

**Performance and size** (Chromium, desktop GPU):

- Module start 0.2 to 0.6 s including `proj.db`; the test scene (11 sites) is placed in about 50 ms.
- A real 3D object layer (Zurich office, two features, 181 MiB of glb): downloaded in about 3 s on a fast connection,
  read by the glTF plugin in 0.5 s, placed in 0.7 s (with the EGM96 grid fetched on the way), drawn as 81 meshes with
  3.5 million triangles; ready after 8.7 s.
- Download: module 22.9 MB (5.8 MB gzip, 3.6 MB brotli), `proj.db` 10.7 MB (2.0 MB gzip), fetched once and cached
  separately (D26); grids on demand (EGM96 2.2 MB, NAD27 and NAVD88 sets a few MB). `-Oz` at link time saves 3 %
  (gzip); a full `-Oz` build and brotli serving remain to be tried.

## Feedback for the proposal

The proposal's model held up: binding by ordinary composition arcs, position plus heading/pitch/roll against the
ellipsoid-normal ENU frame, the consumer choosing the output CRS, and visible failures. Points that came up while
implementing it:

1. **Operation choice depends on the location.** Without an area of interest an engine ranks operations for a whole
   datum: NAD27 at Redlands got a Canadian operation, 12 m off. Our engine interface takes an area of interest at the
   anchor. The proposal could say that the engine chooses operations for the data's location.
2. **Engines should report what is missing, not only that they failed.** A better operation is often skipped for a
   missing grid file. Our results list the skipped operations and the missing grids, so a client can fetch them and
   retry (the browser does). Worth a sentence next to requirements 21 and 28.
3. **Approximate operations must not pass silently.** PROJ offers "ballpark" operations with unknown accuracy when
   nothing better is usable; placing with them is metres off. We disable them, so the case is reported. The proposal's
   accuracy requirement implies this; an explicit note would help implementers.
4. **The EGM2008 default for 2D CRSs is heavy for web clients.** It needs a 77 MB grid; our browser build reports such
   anchors as unsupported. Options: let consumers report the default as unsupported (as we do), or allow a lighter
   geoid model as an approximation with its accuracy stated.
5. **Projected output CRSs with heights need care.** Transforming directly into a projected CRS with an ellipsoidal
   height axis can keep the source height across a datum shift (21 m for NAD27 with PROJ). Going through the output's
   geographic 3D CRS, then projecting, is correct. Since F(x) already works through geographic 3D, the proposal could
   say the same for outputs.
6. **One matrix per anchor is a linearization.** In an ENU output it is exact to well under a micrometre for a 1 km
   model; in Web Mercator it is 25 mm off at 1 km. The proposal's two-tier precision (double placement, float detail)
   works, but could state that consumers may need per-vertex placement for large models in non-metric outputs.
7. **The anchor's own transform.** The post-placement adjustment in the working chart is still a candidate; we report
   it as unsupported. A decision either way would settle the transform order.
8. **Naming.** The schema table says `GeospatialCRS`, several USDA examples still use `CoordinateReferenceSystem`.

Two observations from the consumer side, not about the proposal itself:

- **Display systems have their own height conventions.** The Maps SDK draws all heights as ellipsoidal, while its
  terrain service stores orthometric heights. Correct ellipsoidal placements therefore float above the terrain by the
  geoid height (47 m in Zurich). The app therefore places in the scene's height model (D28): with terrain, the output
  CRS has EGM96 heights (EPSG:4326+5773), so placement and drawing agree and an EGM96 layer comes out at its own
  heights. The proposal's consumer-chosen output CRS covers this: the consumer's CRS includes its vertical part.
- **Assets without the schemas.** ArcGIS 3D object layers serve plain glb models with a CRS and transform in the layer's
  metadata. A generated wrapper layer that references each model and binds it with `GeospatialCRSBindingAPI` places
  them without touching the files, which supports the proposal's minimal-disruption principle.

## Findings on OpenUSD for the web

- **Hydra and usdImaging build for WebAssembly with few changes** (`WASM_GAP.md`): `build_usd.py` options, no default
  Hgi on WebAssembly, two test fixes, missing `override` markers. Clang on wasm32 also exposed four upstream bugs
  (stack walking, a 32-bit shift in the TetMesh face hash, 64-bit printf formats, an out-of-range float-to-int
  conversion in crate compression) and warnings about `typeid` operands with side effects. These are separate
  commits, ready to become upstream pull requests.
- **Static builds must link plugins whole-archive.** Only linked libraries have their `plugInfo.json` embedded, and
  USD whole-archives only its own libraries; plugins built outside `pxr/` lose their registrations otherwise.
- **Env settings are read when a library loads**, before `main`, and WebAssembly processes do not see a test runner's
  environment: anything configured by environment must have an API path too.
- **Scene index plugins run after flattening**, so a geospatial scene index sees world transforms and must carry the
  binding to descendants through a flattened data source.
- **Hydra-only changes are invisible to UsdGeom tools:** usdview's bounding box and transforms show the unplaced scene.

## Findings on the Maps SDK

- Global render space is ECEF on a 6378137 m sphere, not EPSG:4978. Anchors are therefore output in EPSG:4979 and mapped
  through the SDK's own geographic-to-render function, with a float32-exact origin per mesh (D13).
- Local Web Mercator views are not metric (1 m = 1 Mercator unit); placement includes the projection scale.
- Under `Cross-Origin-Embedder-Policy: require-corp` the SDK's CDN worker is blocked; its assets are self-hosted.
  ArcGIS Online feature services and their assets answer with CORS.
- `query3D` returns a 3D object layer's models only with `returnGeometry=true`, and layers without the Query capability
  (e.g. Urban model layers) cannot be read at all.

## Next

- Upstream: the WebAssembly imaging fixes and the bug fixes as separate pull requests; the geospatial libraries
  once the proposal is accepted.
- Size: a full `-Oz` build and brotli serving.
- Open items in `NEXT_STEPS.md`: transform semantics of rotated 3D object layer features, a thread pool for large
  models, heights in local views, the anchor's own transform, instancing under anchors, surveyed control points.
- E1: the Esri Projection Engine as a second engine plugin.
