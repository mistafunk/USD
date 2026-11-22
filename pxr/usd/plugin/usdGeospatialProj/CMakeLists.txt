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

# A WebAssembly PROJ has no data files of its own: tests running under Node
# mount PROJ's installed data directory (proj.db, grids) with NODEFS and
# point PROJ_DATA at it. Other tests using this plugin link the same file.
if (EMSCRIPTEN)
    get_filename_component(projDataDir "${PROJ_DIR}/../../../share/proj"
                           ABSOLUTE)
    set(USDGEOSPATIALPROJ_TEST_PRE_JS
        "${CMAKE_CURRENT_BINARY_DIR}/projData.pre.js" CACHE INTERNAL "")
    file(WRITE "${USDGEOSPATIALPROJ_TEST_PRE_JS}"
"Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
    FS.mkdir('/proj');
    FS.mount(FS.filesystems.NODEFS, { root: '${projDataDir}' }, '/proj');
    ENV.PROJ_DATA = '/proj';
});
")
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

if (EMSCRIPTEN)
    target_link_options(testUsdGeospatialProjPlacement PRIVATE
        "SHELL:--pre-js '${USDGEOSPATIALPROJ_TEST_PRE_JS}'")
endif()

pxr_register_test(testUsdGeospatialProjPlacement
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeospatialProjPlacement"
    TESTENV testenv/testUsdGeospatialProjPlacement
    EXPECTED_RETURN_CODE 0
)
