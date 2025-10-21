set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGeospatial)

find_package(PROJ REQUIRED CONFIG)

pxr_library(usdGeospatial
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        plug
        vt
        sdf
        trace
        usd
        usdGeom
        work
        PROJ::proj

    PUBLIC_CLASSES

    PUBLIC_HEADERS
        api.h

    PYMODULE_CPPFILES
        wrapCoordinateReferenceSystem.cpp
        wrapBindingAPI.cpp
        wrapTokens.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
)

pxr_build_test(testUsdGeospatial
    LIBRARIES
        usd
        usdGeom
        usdGeospatial
    CPPFILES
        testenv/testUsdGeospatial.cpp
)

pxr_install_test_dir(
    SRC testenv/testUsdGeospatial.testenv
    DEST testUsdGeospatial
)

pxr_register_test(testUsdGeospatial
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatial"
    EXPECTED_RETURN_CODE 0
)
