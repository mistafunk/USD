set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usd3dTiles)

pxr_plugin(usd3dTiles
    LIBRARIES
        js
        usd
        usdGeom
        usdGeospatial

    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}

    PRIVATE_HEADERS
        api.h
        debugCodes.h
        fileFormat.h
        sdfUtils.h
        tileset.h

    CPPFILES
        debugCodes.cpp
        fileFormat.cpp
        sdfUtils.cpp
        tileset.cpp

    PYMODULE_CPPFILES
        module.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
)

pxr_build_test(testUsd3dTiles
    LIBRARIES
        #tf
        usd
        usdGeospatial

    CPPFILES
        testenv/testUsd3dTiles.cpp
)

pxr_register_test(testUsd3dTiles
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsd3dTiles"
    TESTENV testenv/testUsd3dTiles.testenv
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testUsd3dTilesSubtrees
    LIBRARIES
        usd
        usd3dTiles

    CPPFILES
        testenv/testUsd3dTilesSubtrees.cpp
)

pxr_register_test(testUsd3dTilesSubtrees
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsd3dTilesSubtrees"
    TESTENV testenv/testUsd3dTilesSubtrees.testenv
    EXPECTED_RETURN_CODE 0
)
