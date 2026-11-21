set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGeospatial)

pxr_library(usdGeospatial
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        vt
        sdf
        usd

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
