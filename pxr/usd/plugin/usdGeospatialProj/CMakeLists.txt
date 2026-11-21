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

pxr_build_test(testUsdGeospatialProjPlacement
    LIBRARIES
        tf
        js
        usd
        usdGeospatial
    CPPFILES
        testenv/testUsdGeospatialProjPlacement.cpp
)

pxr_register_test(testUsdGeospatialProjPlacement
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialProjPlacement"
    TESTENV testenv/testUsdGeospatialProjPlacement
    EXPECTED_RETURN_CODE 0
)
