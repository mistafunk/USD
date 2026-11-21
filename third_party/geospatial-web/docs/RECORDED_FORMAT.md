# Recorded placement data (version 1)

What the offline reference writes today (W1) and what the C3 scene index observer must produce, so the web app
draws either without changes. One directory holds `buffers.bin` and one manifest per output:

| File | Output CRS | Matrix frame | Used by |
|---|---|---|---|
| `manifest-global.json` | EPSG:4979 | `enu`: metres east/north/up on the output ellipsoid at the anchor | global SceneView (D13) |
| `manifest-local-3857.json` | EPSG:3857 (3D) | `crs`: output CRS units (x, y, ellipsoidal height) relative to the anchor | local Web Mercator SceneView |

The reference writes them to `app/public/recorded/` with `python tools/geo.py reference -- export`; the C3 observer
(`module/geospatialRecord`) writes the same layout to `app/public/recorded-cpp/` with `python tools/geo.py record`
(app: `?src=recorded-cpp`). `reference -- compare` checks one against the other.

## buffers.bin

Little-endian, every view 4-byte aligned, referenced by byte offset from the manifest:
`points` and `normals` are float32 xyz triples in mesh-local stage units; `indices` are uint32 triangle indices.
The reference writes flat-shaded, de-indexed triangles (indices are 0..n-1); the observer may share vertices.

## Manifest

```json
{
  "format": "geospatial-web-recorded", "version": 1,
  "stage": "scene.usda",
  "output": { "name": "global", "crs": "EPSG:4979", "wkt": "...", "frame": "enu" },
  "engine": { "name": "pyproj", "version": "3.8.0", "proj": "9.8.1" },  // C++: { "name": "proj" }
  "stageMetersPerUnit": 1.0, "stageUpAxis": "Z",
  "buffers": "buffers.bin",
  "anchors": [ {
    "path": "/World/SiteA", "status": "ok | unsupported | error", "reason": "(when not ok)",
    "sourceCrs": "WGS 84 / UTM zone 11N", "position": [e, n, h], "orientation": [heading, pitch, roll],
    "origin": [lon, lat, h],
    "matrix": [16 numbers: anchor model frame (stage units) to the anchor's frame],
    "operation": "PROJ description of the operation used", "accuracy": 0.0
  } ],
  "meshes": [ {
    "path": "/World/SiteA/Building", "anchor": "/World/SiteA", "status": "ok | unplaced | ...",
    "color": [r, g, b],
    "matrix": [16 numbers, column-major, Float64],
    "linearizationError": 0.0,
    "points":  { "byteOffset": 0,   "count": 36 },
    "normals": { "byteOffset": 432, "count": 36 },
    "indices": { "byteOffset": 864, "count": 36 }
  } ],
  "controlPoints": [ {
    "path": "/World/SiteA/ControlPoints/NE", "anchor": "/World/SiteA", "local": [20, 10, 0],
    "reference": { "crs": "EPSG:4979", "position": [lon, lat, h] }
  } ],
  "testCases": [ { "anchor": "/World/SiteA", "expect": "pass | fail | unsupported", "description": "...",
                   "expectLocal": "(optional: outcome in a local view with matrix placement)" } ]
}
```

- **`output.wkt`** is the CRS placement used: a projected output is promoted to 3D (ellipsoidal height axis).
- **`anchors[].origin`** is the anchor in the output CRS: lon, lat (degrees) and ellipsoidal height for EPSG:4979;
  x, y, height for a projected output.
- **`meshes[].matrix`** maps a mesh-local point (stage units) into the anchor's frame: the linearization at the anchor
  of the proposal's point map `T_src->out(F(x * D))`, where `D` is the mesh's transform relative to its anchor. It
  includes `metersPerUnit`, the up-axis mapping, heading/pitch/roll, the source ellipsoid's ENU basis, the datum and
  projection. Its translation is relative to `origin` (zero for a mesh at the anchor).
- **`linearizationError`** is the largest distance, over the mesh's points, between the exact point map and the matrix,
  in the matrix frame's units (metres for `enu`). It is the T5 measurement.
- **`engine`, `operation`, `accuracy`** name what produced the result (proposal requirement 28). `accuracy` is in
  metres, `null` when unknown.
- **`anchors[].matrix`** is the same linearization for the anchor itself (`D` = identity); the W2 harness places
  control points with it.
- **`time`** (optional, top level): the time the scene was evaluated at (`geotest:time` in the root layer's
  `customLayerData`).
- **`anchors[].skippedOperations`** (optional): more accurate operations the engine skipped because their grids are
  missing (T3, D22).
- **`controlPoints`, `testCases`** are test data, read from the stage's `geotest:` attributes
  (`geotest:referencePosition`, `geotest:referenceCrs` on control point prims under an anchor; `geotest:expect`,
  `geotest:description` on the anchor). `local` is the control point in its anchor's model frame (stage units).
  The C3 observer does not need to produce these; the harness can read them from the stage.
- A mesh whose anchor is not `ok` is listed but not drawn; the anchor's `reason` says why. A mesh without any CRS
  binding is `unplaced`.

## How the app uses it

`app/src/recorded.ts`, per mesh, all in Float64 until the final cast:

- `enu` (global view): `frame` = derivative of the SDK's geographic-to-render mapping at `origin`, from
  `toRenderCoordinates` at +-1 m along WGS84 east, north and up. Not `renderCoordinateTransformAt`: that frame is metric
  on the SDK's 6378137 m sphere and disagrees with lon/lat-placed content by about 0.1-0.3 % (2.4 cm over 22 m in
  Redlands).
- `crs` (local view): `frame` = translate(`origin`); the view's spatial reference must be the output CRS
  (Web Mercator is reported as wkid 102100, latestWkid 3857).
- `full = frame * matrix`, split into a float32-exact origin and a Float64 remainder; the RenderNode draws
  `viewMatrix * translate(origin) * remainder`.
