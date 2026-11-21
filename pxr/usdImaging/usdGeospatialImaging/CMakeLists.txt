set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdGeospatialImaging)

pxr_library(usdGeospatialImaging
    LIBRARIES
        tf
        gf
        vt
        sdf
        usd
        usdGeom
        usdGeospatial
        hd
        usdImaging

    PUBLIC_CLASSES
        anchorSchema
        bindingAPIAdapter
        bindingSchema
        meshObserver
        placementSceneIndex
        placementSchema
        sceneIndexPlugin
        tokens

    PUBLIC_HEADERS
        api.h

    RESOURCE_FILES
        plugInfo.json
)

# The test places anchors with PROJ (the expected values come from pyproj).
if (TARGET usdGeospatialProj)
    set(testPluginLibraries "")
    if (NOT BUILD_SHARED_LIBS)
        set(testPluginLibraries usdGeospatialProj)
    endif()

    pxr_build_test(testUsdGeospatialImagingPlacement
        LIBRARIES
            tf
            gf
            js
            usd
            usdGeospatial
            hd
            usdImaging
            usdGeospatialImaging
            ${testPluginLibraries}
        CPPFILES
            testenv/testUsdGeospatialImagingPlacement.cpp
    )

    pxr_register_test(testUsdGeospatialImagingPlacement
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialImagingPlacement"
        TESTENV testenv/testUsdGeospatialImagingPlacement
        EXPECTED_RETURN_CODE 0
    )

    # What usdview sees: the scene index plugin, enabled by the environment,
    # with Site A's origin (manifest-global.json) as display origin. Env
    # settings are read when the library loads, so the test cannot set them
    # itself, and a WebAssembly build does not see the runner's environment.
    if (NOT EMSCRIPTEN)
        pxr_register_test(testUsdGeospatialImagingPlugin
            COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialImagingPlacement --plugin"
            TESTENV testenv/testUsdGeospatialImagingPlacement
            EXPECTED_RETURN_CODE 0
            ENV
                USDGEOSPATIALIMAGING_OUTPUT_CRS=EPSG:4979
                "USDGEOSPATIALIMAGING_DISPLAY_ORIGIN=-117.19549997299366,34.05649998208545,0"
        )
    endif()
endif()
