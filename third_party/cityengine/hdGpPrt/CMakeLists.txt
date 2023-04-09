set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdGpPrt)

set(optionalPublicClasses "")

pxr_library(hdGpPrt
    LIBRARIES
        hd
		hdGp
        hf
        ${TBB_tbb_LIBRARY}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}

    PUBLIC_CLASSES
        prtProcedural

    RESOURCE_FILES
        plugInfo.json
)
