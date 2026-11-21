# C++ side of the geospatial web prototype, built from the root CMakeLists.txt
# when PXR_BUILD_GEOSPATIAL_WEB is ON.
#
# - geospatialRecord (C3): writes what the scene index observer sees in the
#   recorded format, so the web app can draw the C++ result.
# - The embind module and its worker wrapper follow in C4.

# Engine plugins load at runtime in shared builds; static builds link them.
set(enginePlugins "")
if (NOT BUILD_SHARED_LIBS)
    set(enginePlugins usdGeospatialProj)
endif()

pxr_cpp_bin(geospatialRecord
    LIBRARIES
        tf
        gf
        js
        sdf
        usd
        usdGeom
        usdGeospatial
        hd
        usdImaging
        usdGeospatialImaging
        ${enginePlugins}
)
