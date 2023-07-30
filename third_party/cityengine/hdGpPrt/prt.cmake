include(FetchContent)


### environment

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(PRT_WINDOWS 1)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(PRT_LINUX 1)
endif ()


### look for the PRT libraries

# if prt_DIR is not provided, download PRT from its github home
if (NOT prt_DIR)
	if (PRT_WINDOWS)
		set(PRT_OS "win10")
		set(PRT_TC "vc1427")
	elseif (PRT_LINUX)
		set(PRT_OS "rhel7")
		set(PRT_TC "gcc93")
	endif ()

	set(PRT_VERSION "3.0.8905")
	set(PRT_CLS "${PRT_OS}-${PRT_TC}-x86_64-rel-opt")
	set(PRT_URL "https://github.com/esri/cityengine-sdk/releases/download/${PRT_VERSION}/esri_ce_sdk-${PRT_VERSION}-${PRT_CLS}.zip")

	FetchContent_Declare(prt URL ${PRT_URL} DOWNLOAD_EXTRACT_TIMESTAMP 1)
	FetchContent_GetProperties(prt)
	if (NOT prt_POPULATED)
		message(STATUS "Fetching PRT from ${PRT_URL}...")
		FetchContent_Populate(prt)
	endif ()

	set(prt_DIR "${prt_SOURCE_DIR}/cmake")
endif ()

find_package(prt CONFIG REQUIRED)
set(CESDK_VERSION "cesdk_${PRT_VERSION_MAJOR}_${PRT_VERSION_MINOR}_${PRT_VERSION_MICRO}")
message(STATUS "Using prt_DIR = ${prt_DIR} with version ${PRT_VERSION_MAJOR}.${PRT_VERSION_MINOR}.${PRT_VERSION_MICRO}")

function(prt_add_dependency TGT)
	target_compile_definitions(${TGT} PRIVATE -DPRT_VERSION_MAJOR=${PRT_VERSION_MAJOR} -DPRT_VERSION_MINOR=${PRT_VERSION_MINOR})
	target_include_directories(${TGT} PRIVATE ${PRT_INCLUDE_PATH})
	target_link_libraries(${TGT} ${PRT_LINK_LIBRARIES})
endfunction()

set(_PRT_LIBRARIES ${PRT_LIBRARIES})
list(FILTER _PRT_LIBRARIES EXCLUDE REGEX "\\.lib")
install(FILES ${_PRT_LIBRARIES} DESTINATION lib)

install(FILES ${PRT_EXT_LIBRARIES} DESTINATION lib/prt_ext)
install(DIRECTORY "${PRT_EXTENSION_PATH}/usd" DESTINATION lib/prt_ext) # USD resource files
