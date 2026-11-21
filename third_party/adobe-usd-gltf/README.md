This is a minimally modified copy of

https://github.com/adobe/USD-Fileformat-plugins/tree/main/gltf

and parts of

https://github.com/adobe/USD-Fileformat-plugins/tree/main/utils

from the main branch as of 2025-12-07. Version 1.2.0.

Ported from the geospatial-web fork's `3dtiles-prototype` branch (commits 55e91dc4c, 87f26b314: glTF point
geometry) to v26.08, as `third_party/adobe-usd-gltf` (D24). Local changes for the geospatial web prototype:

- Built from the root `CMakeLists.txt` with `PXR_BUILD_ADOBE_GLTF`, natively and for WebAssembly.
- tinygltf v2.9.7 comes from `geo.py bootstrap <target> --steps tinygltf` (pinned), not CMake FetchContent.
- OpenImageIO is optional (`ADOBE_GLTF_HAS_OIIO` when USD's hioOiio is built): without it `Image::read`,
  `Image::write` and `Image::convertImageToPng` return false, so textures stay encoded files; the plugin no longer
  requires hioOiio in plugInfo.json.
- No Draco and no Python module.
- `layerRead.cpp`: two debug messages printed size_t indices with `%d`; now `%zu`.
- Known upstream issue, export only: `gltfAnisotropy.cpp` passes a float roughness to a bool parameter of
  `processAnisotropyPixels` (Clang `-Wimplicit-conversion-floating-point-to-bool`). Vendored warning kinds are
  silenced for this target in `CMakeLists.txt`.

Tests: `testUsdGltfFileFormat` (native, shared builds; `geo.py test native-debug -- -R Gltf`) checks the file format
registration, a glb and a glTF JSON + .bin box (stage metadata, exact points and triangles), node translation,
rotation and scale, and referencing a glb from a USD layer. Fixtures are written by `testenv/makeFixtures.py`.
