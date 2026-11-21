set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGltf)

# geospatial-web: tinygltf from geo.py (deps/<target>/include/tinygltf),
# zlib from the system or emscripten's port, OpenImageIO only if USD's hioOiio
# is built (else images stay encoded), no Draco and no Python module.
find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h PATH_SUFFIXES tinygltf)
if (NOT TINYGLTF_INCLUDE_DIR)
    message(FATAL_ERROR "tinygltf not found; run: geo.py bootstrap <target> "
            "--steps tinygltf")
endif()

if (EMSCRIPTEN)
    set(zlibLibraries "")
else()
    find_package(ZLIB REQUIRED)
    set(zlibLibraries ZLIB::ZLIB)
endif()

pxr_plugin(usdGltf
    LIBRARIES
        #tf
        #work
        #sdf
        hio
        usd
        usdGeom
        usdLux
        usdRender
        usdShade
        usdSkel
        usdVol
        ${zlibLibraries}


    PRIVATE_HEADERS
        api.h
        debugCodes.h
        fileFormat.h
        gltf.h
        gltfAnisotropy.h
        gltfExport.h
        gltfImport.h
        gltfResolver.h
        gltfSpecGloss.h
        importGltfContext.h
        fileformatutils/api.h
        fileformatutils/common.h
        fileformatutils/debugCodes.h
        fileformatutils/dictencoder.h
        fileformatutils/geometry.h
        fileformatutils/images.h
        fileformatutils/layerRead.h
        fileformatutils/layerReadMaterial.h
        fileformatutils/layerWriteMaterial.h
        fileformatutils/layerWriteOpenPBR.h
        fileformatutils/layerWriteSdfData.h
        fileformatutils/materials.h
        fileformatutils/neuralAssetsHelper.h
        fileformatutils/resolver.h
        fileformatutils/sdfMaterialUtils.h
        fileformatutils/sdfUtils.h
        fileformatutils/transforms.h
        fileformatutils/usdData.h

    CPPFILES
        debugCodes.cpp
        fileFormat.cpp
        gltf.cpp
        gltfAnisotropy.cpp
        gltfExport.cpp
        gltfImport.cpp
        gltfResolver.cpp
        gltfSpecGloss.cpp
        tinygltf.cpp
        fileformatutils/assetresolver.cpp
        fileformatutils/common.cpp
        fileformatutils/dictencoder.cpp
        fileformatutils/geometry.cpp
        fileformatutils/images.cpp
        fileformatutils/layerRead.cpp
        fileformatutils/layerReadMaterial.cpp
        fileformatutils/layerWriteMaterial.cpp
        fileformatutils/layerWriteOpenPBR.cpp
        fileformatutils/layerWriteSdfData.cpp
        fileformatutils/layerWriteShared.cpp
        fileformatutils/materials.cpp
        fileformatutils/neuralAssetsHelper.cpp
        fileformatutils/resolver.cpp
        fileformatutils/sdfMaterialUtils.cpp
        fileformatutils/sdfUtils.cpp
        fileformatutils/transforms.cpp
        fileformatutils/usdData.cpp


    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        overview.dox
)

# Private: static builds export a library's INCLUDE_DIRS, and these are only
# needed to compile the plugin (tinygltf's path normalized, as the presets
# give it relative to the source tree).
file(REAL_PATH "${TINYGLTF_INCLUDE_DIR}" tinygltfIncludeDir)
target_include_directories(usdGltf PRIVATE
    ${tinygltfIncludeDir} ${CMAKE_CURRENT_SOURCE_DIR})

# Vendored code: keep its known warning kinds out of the build log.
if (NOT MSVC)
    target_compile_options(usdGltf PRIVATE
        -Wno-sign-compare -Wno-format-extra-args -Wno-unused-variable)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(usdGltf PRIVATE
        -Wno-bitwise-instead-of-logical -Wno-unused-but-set-variable
        -Wno-implicit-const-int-float-conversion
        -Wno-implicit-conversion-floating-point-to-bool)
endif()

if (EMSCRIPTEN)
    target_compile_options(usdGltf PRIVATE "SHELL:-sUSE_ZLIB=1")
    target_link_options(usdGltf PUBLIC "SHELL:-sUSE_ZLIB=1")
endif()

if (TARGET hioOiio)
    target_compile_definitions(usdGltf PRIVATE ADOBE_GLTF_HAS_OIIO)
    target_link_libraries(usdGltf PRIVATE ${OIIO_OpenImageIO_LIBRARY})
endif()

# Basic confidence tests (native: the plugin is found through plugInfo.json).
# Fixtures: testenv/makeFixtures.py.
if (BUILD_SHARED_LIBS)
    pxr_build_test(testUsdGltfFileFormat
        LIBRARIES
            tf
            gf
            sdf
            usd
            usdGeom
        CPPFILES
            testenv/testUsdGltfFileFormat.cpp
    )

    pxr_register_test(testUsdGltfFileFormat
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGltfFileFormat"
        TESTENV testenv/testUsdGltfFileFormat
        EXPECTED_RETURN_CODE 0
    )
endif()
