# Geospatial CRS proposal: what we implement

Source: https://github.com/Tamrat-B/OpenUSD-proposals/pull/1 (head 4120379fa12473b54a8f8e11999a1cefa5443614,
2026-10-22). The proposal is still under review; re-check before C0 and keep field names in one tokens file.

## Schemas

- **`GeospatialCRS`** (concrete typed prim, D14; the proposal's USDA examples still say `CoordinateReferenceSystem`):
  `uniform token crs:wkt`, an OGC WKT2 (ISO 19162:2019) string.
  Token, not string, for cheap comparison. Can live in the stage or in a shared CRS library layer.
- **`GeospatialCRSBindingAPI`** (applied, on a directly bound model anchor):
  - binding: the anchor composes in a `GeospatialCRS` prim through an ordinary USD reference (or inherit, or any Core
    arc), so `crs:wkt` is read on the anchor itself after composition; no relationship. Inherited down the hierarchy,
  - `double3 crs:position` (varying, no fallback): absolute position in the source WKT's units and height reference.
    Projected: easting, northing, up. Geographic 3D: lon, lat, height. Geocentric: X, Y, Z.
    Tuple order is independent of WKT axis order and stage `upAxis`.
  - `double3 crs:orientation` (varying, fallback 0,0,0): heading, pitch, roll in degrees vs ellipsoid-normal ENU
    at the position (PROJ topocentric construction).
  - No `crs:scale`; scaling comes from WKT + position via the full point map. Object scale uses ordinary xformOps.

## Behaviour we must get right

- Direct vs inherited binding, nearest-ancestor walk. **A broken direct binding is an error with no fallback** to the parent.
- Missing/blocked/non-finite position is an error. Placement fields on inherited-only descendants are invalid.
- 2D CRS: height from default vertical reference EGM2008, with a warning.
- Unsupported axis directions (initial profile: east/north/up and geocentric X/Y/Z) fail visibly.
- Placement **F(x)**: position to geographic 3D, ENU basis, heading/pitch/roll, `metersPerUnit` and `upAxis`
  mapping, then the anchor's ordinary xformOps. Time-sampled position; slerped orientation.
- Output CRS is **chosen by the consumer** (requirement 16); without one, the CRS bound to `defaultPrim`, else fail.
- Failures visible (requirement 21); every result names the engine, operation and accuracy (requirement 28).
- Precision: double for placement, `point3f` for local detail.

## Out of scope for the prototype

`GeospatialDataSource` measurement association, Profiles dependency claims, WKT normalization checks,
coordinate epochs, grids in the browser.

## Fixtures

The proposal's Eiffel Tower, UTM 11N and California zone 5 + NAVD88 examples, plus its Appendix C counterexamples.
