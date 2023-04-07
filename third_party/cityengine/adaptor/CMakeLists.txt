set(PXR_PREFIX esri/usdImaging)
set(PXR_PACKAGE usdPrtImaging)

pxr_library(usdPrtImaging
    LIBRARIES
        gf
        tf
        plug
        trace
        vt
        work
        geomUtil
        hd
        hdar
        hio
        pxOsd
        sdf
        usd
        usdGeom
        usdLux
        usdRender
        usdShade
        usdVol
        ar
        ${TBB_tbb_LIBRARY}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}

    PUBLIC_CLASSES
        ruleAdapter

    RESOURCE_FILES
        plugInfo.json
)

