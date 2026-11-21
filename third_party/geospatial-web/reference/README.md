# reference

Offline reference (plan option D): evaluates the proposal's model placement with usd-core and pyproj, independently of
the C++ library. Ground truth for the tests, and the source of the app's recorded data until the WASM module exists.

```
python ../tools/geo.py reference -- fixtures   # writes ../testdata/w1/*.usda
python ../tools/geo.py reference -- export     # writes ../app/public/recorded/ (format: ../docs/RECORDED_FORMAT.md)
python ../tools/geo.py reference -- check      # mast on the building corner, linearization error, within 1 mm
```

`geo.py` creates `<workspace>/venv-reference` with Python >= 3.12 and the pins in `requirements.txt`
(usd-core 26.8, pyproj 3.8.0 with PROJ 9.8.1, numpy).

The schemas are not registered in usd-core, so the reference reads them by name (`GeospatialCRSBindingAPI` in
`apiSchemas`, composed `crs:wkt`, `crs:position`, `crs:orientation`). Supported: 3D source CRSs with east/north/up or
geocentric axes, descendant transforms, `metersPerUnit` and `upAxis`. Reported as unsupported: 2D CRSs (EGM2008 default
height needs a geoid grid), other axis directions, the anchor's own ordinary transform, CRS pairs without a real
coordinate operation (ballpark operations are disabled).
