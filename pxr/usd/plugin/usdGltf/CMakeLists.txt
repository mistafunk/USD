set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGltf)

# Optional Draco support
set(optionalLibs "")
set(optionalIncludeDirs "")
set(optionalDefines "")

if (GLTF_HAS_DRACO)
    list(APPEND optionalLibs ${DRACO_LIBRARY})
    list(APPEND optionalIncludeDirs ${DRACO_INCLUDES})
    list(APPEND optionalDefines GLTF_ENABLE_DRACO)
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
        z
        ${optionalLibs}

    INCLUDE_DIRS
        ${TINYGLTF_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${optionalIncludeDirs}

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

    PYMODULE_CPPFILES
        module.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        overview.dox
)

# Add compile definitions if needed
if(optionalDefines)
    target_compile_definitions(usdGltf PRIVATE ${optionalDefines})
endif()

if(PXR_BUILD_OPENIMAGEIO_PLUGIN)
    target_link_libraries(usdGltf PRIVATE ${OIIO_OpenImageIO_LIBRARY})
endif()