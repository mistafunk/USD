set(PXR_PREFIX esri/usd)
set(PXR_PACKAGE usdPrt)

pxr_library(usdPrt
	LIBRARIES
		tf
		sdf
		usd
		vt

    INCLUDE_DIRS
        ${Boost_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIRS}

    PUBLIC_HEADERS
        api.h

	PUBLIC_CLASSES
		ruleAssignment

    PYMODULE_CPPFILES
        wrapRuleAssignment.cpp
        wrapTokens.cpp

	RESOURCE_FILES
	    generatedSchema.usda
	    plugInfo.json
	    schema.usda:usdPrt/schema.usda
)
