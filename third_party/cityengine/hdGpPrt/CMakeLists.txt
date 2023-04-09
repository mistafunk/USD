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

	CPPFILES
        prtProcedural.cpp
		prtContext.cpp
		prtUtilities.cpp

    RESOURCE_FILES
        plugInfo.json
)

include(prt.cmake)
prt_add_dependency(hdGpPrt)

set_target_properties(hdGpPrt PROPERTIES CXX_STANDARD 17)
