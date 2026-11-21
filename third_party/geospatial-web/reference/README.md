# reference

Offline reference (plan option D): evaluates the proposal's model placement with usd-core and pyproj, independently of
the C++ library. Ground truth for the tests, and the source of the app's recorded data until the WASM module exists.

```
python ../tools/geo.py reference -- fixtures   # writes ../testdata/scene/*.usda
python ../tools/geo.py reference -- export     # writes ../app/public/recorded/ (format: ../docs/RECORDED_FORMAT.md)
python ../tools/geo.py reference -- check      # scores control points with the exact point map; export checks
python ../tools/geo.py reference -- proj-expected  # C++ PROJ engine test fixtures + pyproj expectations
```

`geo.py` creates `<workspace>/venv-reference` with Python >= 3.12 and the pins in `requirements.txt`
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

Control point reference coordinates come from PROJ's topocentric conversion, not from this script's ENU code.

`proj-expected` writes `pxr/usd/plugin/usdGeospatialProj/testenv/testUsdGeospatialProjPlacement/` (stages and
`expected.json`), which `testUsdGeospatialProjPlacement` compares against within 1 mm. Operations are chosen as the C++
engine chooses them (`best_transformer`: first listed operation, no ballpark, operations needing missing grids
discarded), otherwise PROJ may rank a different operation first.
