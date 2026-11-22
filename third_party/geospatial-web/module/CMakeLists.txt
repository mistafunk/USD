# C++ side of the geospatial web prototype, built from the root CMakeLists.txt
# when PXR_BUILD_GEOSPATIAL_WEB is ON.
#
# - geospatialRecord (C3, native): writes what the scene index observer sees
#   in the recorded format, so the web app can draw the C++ result.
# - geospatialModule (C4, WebAssembly): the browser module, with an embind
#   API for the app's worker. Installed with the test fixtures to
#   share/geospatial-web, which the dev server and nginx serve at /usd/.

set(geospatialLibraries
    tf
    gf
    js
    work
    plug
    sdf
    usd
    usdGeom
    usdGeospatial
    hd
    usdImaging
    usdGeospatialImaging
)

if (NOT EMSCRIPTEN)
    # Engine plugins load at runtime in shared builds; static builds link
    # them.
    set(enginePlugins "")
    if (NOT BUILD_SHARED_LIBS)
        set(enginePlugins usdGeospatialProj)
    endif()

    pxr_cpp_bin(geospatialRecord
        LIBRARIES
            ${geospatialLibraries}
            ${enginePlugins}
    )
    return()
endif()

add_executable(geospatialModule geospatialModule.cpp)

# Whole-archive linking of the USD libraries and the engine plugin, so their
# registry functions (TfType, scene index plugins, adapters, the PROJ engine)
# survive, as in extras/usd/examples/wasmFetchResolver.
# File format plugins for served models: glb from ArcGIS 3D object layers
# (third_party/adobe-usd-gltf, D24).
set(formatPlugins "")
if (TARGET usdGltf)
    set(formatPlugins usdGltf)
endif()

_pxr_target_link_libraries(geospatialModule
    ${geospatialLibraries}
    usdGeospatialProj
    ${formatPlugins}
)

# USD whole-archives only its own libraries; a plugin built outside pxr/ is
# linked like any external library, so its registrations (the file format's
# TfType) would be dropped. Pull in all of it.
foreach(plugin ${formatPlugins})
    target_link_options(geospatialModule PRIVATE
        "SHELL:-Wl,--whole-archive $<TARGET_FILE:${plugin}> -Wl,--no-whole-archive")
    add_dependencies(geospatialModule ${plugin})
endforeach()
target_include_directories(geospatialModule
    PRIVATE ${CMAKE_BINARY_DIR}/include)
target_link_libraries(geospatialModule PRIVATE PROJ::proj)

target_link_options(geospatialModule PRIVATE
    "SHELL:-lembind"
    "SHELL:--no-entry"
    "SHELL:-sMODULARIZE=1"
    "SHELL:-sEXPORT_ES6=1"
    "SHELL:-sEXPORT_NAME=createGeospatialModule"
    "SHELL:-sENVIRONMENT=web,worker,node"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,ENV"
    # Node: tests mount host directories with NODEFS.
    "SHELL:-lnodefs.js"
)

set(webDir share/geospatial-web)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/geospatialModule.js
        ${CMAKE_CURRENT_BINARY_DIR}/geospatialModule.wasm
    DESTINATION ${webDir}/module
)

# Precompressed module for servers that send .gz files as is (nginx
# gzip_static, the dev server): 30.7 MB -> 7.2 MB, a third of it proj.db,
# which is trimmed later (D17).
install(CODE "
    set(module \"\${CMAKE_INSTALL_PREFIX}/${webDir}/module/geospatialModule.wasm\")
    file(ARCHIVE_CREATE OUTPUT \"\${module}.gz\" PATHS \"\${module}\"
         FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
")

# Fixtures the browser tests load: the C1 PROJ placement cases (expected
# values from pyproj) and the W1/W2 scene.
install(
    DIRECTORY
        ${PROJECT_SOURCE_DIR}/pxr/usd/plugin/usdGeospatialProj/testenv/testUsdGeospatialProjPlacement/
    DESTINATION ${webDir}/fixtures/c1
)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../testdata/scene/
    DESTINATION ${webDir}/fixtures/scene
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../testdata/arcgis/
    DESTINATION ${webDir}/fixtures/arcgis
)

# PROJ grids for the browser (geo.py bootstrap wasm32 --steps grids), fetched
# on demand by the worker, with an index of what is served.
set(GEOSPATIAL_WEB_GRID_DIR "" CACHE PATH
    "PROJ grids served to the browser (the wasm32 preset sets it)")
set(grids "")
if (GEOSPATIAL_WEB_GRID_DIR)
    file(REAL_PATH "${GEOSPATIAL_WEB_GRID_DIR}" gridDir)
    file(GLOB grids CONFIGURE_DEPENDS ${gridDir}/*.tif)
endif()
set(gridNames "")
foreach(grid ${grids})
    get_filename_component(name ${grid} NAME)
    list(APPEND gridNames "\"${name}\"")
endforeach()
list(SORT gridNames)
list(JOIN gridNames ", " gridList)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/grids-index.json "{ \"grids\": [${gridList}] }\n")
install(FILES ${grids} DESTINATION ${webDir}/grids)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/grids-index.json
    DESTINATION ${webDir}/grids
    RENAME index.json
)

# The scene's files, so the app's worker knows what to fetch.
set(sceneDir ${CMAKE_CURRENT_SOURCE_DIR}/../testdata/scene)
file(GLOB sceneFiles CONFIGURE_DEPENDS RELATIVE ${sceneDir} ${sceneDir}/*.usda)
list(SORT sceneFiles)
list(TRANSFORM sceneFiles PREPEND "\"")
list(TRANSFORM sceneFiles APPEND "\"")
list(JOIN sceneFiles ", " sceneFileList)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/scene-index.json
    "{ \"stage\": \"scene.usda\", \"files\": [${sceneFileList}] }\n")
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/scene-index.json
    DESTINATION ${webDir}/fixtures/scene
    RENAME index.json
)
