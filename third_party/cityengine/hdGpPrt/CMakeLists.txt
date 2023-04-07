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

    PYTHON_CPPFILES
        moduleDeps.cpp

    RESOURCE_FILES
        plugInfo.json
)
