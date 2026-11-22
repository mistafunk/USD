# Provides the ZLIB::ZLIB target before a project finds packages whose CMake
# configs link it without finding it themselves (libtiff 4.7's tiff-config,
# reached through PROJ). Injected with CMAKE_PROJECT_INCLUDE by geo.py's
# dependency builds and the presets; for wasm32, ZLIB_INCLUDE_DIR and
# ZLIB_LIBRARY point to emscripten's zlib port.
if (NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
endif()
