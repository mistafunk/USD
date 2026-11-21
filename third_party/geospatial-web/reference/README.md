# reference

Offline reference (plan option D): evaluates the proposal's model placement with usd-core and pyproj, independently of
the C++ library. Ground truth for the tests, and the source of the app's recorded data until the WASM module exists.

```
python ../tools/geo.py reference -- fixtures   # writes ../testdata/scene/*.usda
python ../tools/geo.py reference -- export     # writes ../app/public/recorded/ (format: ../docs/RECORDED_FORMAT.md)
python ../tools/geo.py reference -- check      # scores control points with the exact point map; export checks
python ../tools/geo.py reference -- proj-expected  # C++ PROJ engine test fixtures + pyproj expectations
python ../tools/geo.py reference -- imaging-expected  # scene + export for the C++ placement scene index test
python ../tools/geo.py reference -- compare    # ../app/public/recorded-cpp (geo.py record) vs the export
```

`geo.py` creates `<workspace>/venv-reference` with the project venv's Python (3.12, `VENV_PYTHON` in `geo.py`;
recreated automatically if it differs) and the pins in `requirements.txt`
(usd-core 26.8, pyproj 3.8.0 with PROJ 9.8.1, numpy).

The schemas are not registered in usd-core, so the reference reads them by name (`GeospatialCRSBindingAPI` in
`apiSchemas`, composed `crs:wkt`, `crs:position`, `crs:orientation`). Supported: 3D source CRSs with east/north/up or
geocentric axes, descendant transforms, `metersPerUnit` and `upAxis`. Reported as unsupported: 2D CRSs (EGM2008 default
height needs a geoid grid), other axis directions, the anchor's own ordinary transform, CRS pairs without a real
coordinate operation (ballpark operations are disabled).

Test scene (`testdata/scene`), one test case per 3DOL, expected outcome in `geotest:expect`:

| Site | Setup | Expected |
|---|---|---|
| A | building + wing in 3D UTM 11N, heading 30, 6 control points | pass |
| B | mast in EPSG:4979 on Site A's NE corner, 4 control points | pass |
| C | box in UTM 11N, control point NE has a planted 5 cm reference error | fail (harness self-test) |
| D | box bound to a 2D CRS (NAD83 / California zone 5, ftUS) | unsupported (EGM2008 height) |
| E | direct binding to a missing CRS prim | unsupported (reported error, T7) |
| F | box in WGS 72 (geographic 3D): Helmert shift to WGS 84 | pass; operation logged (T2) |
| G | box in NAD27 (geographic 3D): NADCON grids missing | pass with the 7 m equation-based operation; skipped grid operations reported (T3, D22) |
| H | box in California zone 5 + NAVD88 (compound) | unsupported (geoid grid, T4) |
| I | 1 km runway in UTM 11N | pass; in a local Web Mercator view with matrix placement fail (`geotest:expectLocal`, T5) |
| J | box in geographic 3D, heading 90 | pass (T8) |
| K | box with orientation (0, 0, 0) at 0 and (90, 30, 0) at 10 | pass at the scene time 5, slerped (T8) |

The scene is evaluated at `geotest:time` (root layer `customLayerData`, 5): the reference, `geospatialRecord`, the C++
imaging test and the browser all set that time. Orientation samples are slerped, by this script's own quaternions.

Control point reference coordinates come from PROJ's topocentric conversion on the source datum's ellipsoid, not from
this script's ENU code; for other datums (F, G) they are then transformed to EPSG:4979 with the engine's policy.

Operations are chosen for the anchor's location (`area_at`, an area of interest around the anchor, as the C++ engine
does). Into a projected output the reference goes through the output's geographic 3D CRS (`output_transformer`):
PROJ's direct operation into a projected CRS with an ellipsoidal height axis keeps the source height through the
datum shift. pyproj is built against the workspace's PROJ (D19; `geo.py reference` does it once).

`proj-expected` writes `pxr/usd/plugin/usdGeospatialProj/testenv/testUsdGeospatialProjPlacement/` (stages and
`expected.json`), which `testUsdGeospatialProjPlacement` compares against within 1 mm. Operations are chosen as the C++
engine chooses them (`best_transformer`: first listed operation, no ballpark, operations needing missing grids
discarded), otherwise PROJ may rank a different operation first.

`imaging-expected` copies the test scene and its export (manifests, `buffers.bin`) to
`pxr/usdImaging/usdGeospatialImaging/testenv/testUsdGeospatialImagingPlacement/`, which
`testUsdGeospatialImagingPlacement` reproduces through usdImaging, the placement scene index and the observer.
`compare` checks a recorded directory (default: the C++ observer's) against the export: statuses, anchor origins
within 1 mm, matrices within 1e-6, identical buffers. Operation names are not compared (pyproj bundles another PROJ).
