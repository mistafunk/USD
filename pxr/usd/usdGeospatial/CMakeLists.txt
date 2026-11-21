set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGeospatial)

pxr_library(usdGeospatial
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        gf
        js
        plug
        vt
        sdf
        usd
        usdGeom

    PUBLIC_CLASSES
        binding
        engine
        engineRegistry
        placement

    PUBLIC_HEADERS
        api.h

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.md
)

pxr_test_scripts(
    testenv/testUsdGeospatialSchemas.py
)

pxr_build_test(testUsdGeospatialSchemasCpp
    LIBRARIES
        tf
        usd
        usdGeom
        usdGeospatial
    CPPFILES
        testenv/testUsdGeospatialSchemas.cpp
)

pxr_register_test(testUsdGeospatialSchemas
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialSchemas"
    TESTENV testenv/testUsdGeospatialSchemas
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeospatialSchemasCpp
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialSchemasCpp"
    TESTENV testenv/testUsdGeospatialSchemas
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testUsdGeospatialResolution
    LIBRARIES
        tf
        usd
        usdGeospatial
    CPPFILES
        testenv/testUsdGeospatialResolution.cpp
)

pxr_register_test(testUsdGeospatialResolution
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialResolution"
    TESTENV testenv/testUsdGeospatialResolution
    EXPECTED_RETURN_CODE 0
)

# The engine registry test needs a test-only engine plugin, which the build
# only supports for shared libraries.
if (BUILD_SHARED_LIBS)
    pxr_build_test_shared_lib(TestUsdGeospatialStubEngine
        INSTALL_PREFIX UsdGeospatialPlugins
        LIBRARIES
            tf
            usdGeospatial
        CPPFILES
            testenv/TestUsdGeospatialStubEngine_plugin.cpp
    )

    pxr_build_test(testUsdGeospatialEngineRegistry
        LIBRARIES
            arch
            tf
            plug
            usd
            usdGeospatial
        CPPFILES
            testenv/testUsdGeospatialEngineRegistry.cpp
    )

    pxr_register_test(testUsdGeospatialEngineRegistry
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialEngineRegistry"
        EXPECTED_RETURN_CODE 0
        ENV
            USDGEOSPATIAL_ENGINE=stub
    )
endif()
