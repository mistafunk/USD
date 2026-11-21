set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGeospatialProj)

pxr_plugin(usdGeospatialProj
    LIBRARIES
        tf
        gf
        usdGeospatial
        PROJ::proj

    CPPFILES
        projEngine.cpp

    RESOURCE_FILES
        plugInfo.json
)

# Plugins are loaded at runtime in shared builds; a static build (WASM) must
# link the plugin, which also embeds its plugInfo.json.
set(testPluginLibraries "")
if (NOT BUILD_SHARED_LIBS)
    set(testPluginLibraries usdGeospatialProj)
endif()

pxr_build_test(testUsdGeospatialProjPlacement
    LIBRARIES
        tf
        js
        usd
        usdGeospatial
        ${testPluginLibraries}
    CPPFILES
        testenv/testUsdGeospatialProjPlacement.cpp
)

pxr_register_test(testUsdGeospatialProjPlacement
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialProjPlacement"
    TESTENV testenv/testUsdGeospatialProjPlacement
    EXPECTED_RETURN_CODE 0
)
