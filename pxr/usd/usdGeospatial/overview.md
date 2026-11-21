/*!
\page usdGeospatial_page_front UsdGeospatial : Geospatial coordinate reference systems
\if ( PIXAR_MFB_BUILD )
\mainpage UsdGeospatial : Geospatial coordinate reference systems
\endif

UsdGeospatial implements the schemas of the geospatial CRS proposal
(https://github.com/Tamrat-B/OpenUSD-proposals/pull/1):

- **UsdGeospatialCRS** (`GeospatialCRS`) defines a coordinate reference
  system as a prim, with an OGC WKT2 (ISO 19162:2019) string in `crs:wkt`.
  Definitions can be shared in library layers.
- **UsdGeospatialCRSBindingAPI** (`GeospatialCRSBindingAPI`) makes an
  Xformable a directly bound model anchor. The anchor composes in a
  `GeospatialCRS` prim (usually with a reference), so its composed `crs:wkt`
  is its CRS; `crs:position` locates the model origin in that CRS and
  `crs:orientation` gives heading, pitch and roll against the local
  ellipsoid-normal east/north/up frame. Descendants inherit the binding
  until the next direct binding.

The proposal is still under review, so schema and property names may change.
Binding resolution, placement evaluation and coordinate operations build on
these schemas and are added separately.
*/
