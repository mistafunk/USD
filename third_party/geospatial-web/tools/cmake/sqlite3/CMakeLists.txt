# Builds the SQLite amalgamation for PROJ, natively and with emscripten.
# geo.py configures this with -DSQLITE_SOURCE_DIR=<unpacked amalgamation>.
cmake_minimum_required(VERSION 3.27)
project(sqlite3 C)

if(NOT SQLITE_SOURCE_DIR)
    message(FATAL_ERROR "Set SQLITE_SOURCE_DIR to the unpacked amalgamation")
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

add_library(sqlite3 STATIC ${SQLITE_SOURCE_DIR}/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sqlite3 PUBLIC
    $<BUILD_INTERFACE:${SQLITE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_OMIT_DEPRECATED)
if(EMSCRIPTEN)
    # No dlopen in the browser build.
    target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
endif()
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(sqlite3 PUBLIC m)
endif()

install(TARGETS sqlite3)
install(FILES ${SQLITE_SOURCE_DIR}/sqlite3.h ${SQLITE_SOURCE_DIR}/sqlite3ext.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# PROJ runs the sqlite3 shell at build time to generate proj.db, so the
# native build also installs it. The WASM build of PROJ uses the native one.
if(NOT EMSCRIPTEN)
    add_executable(sqlite3_shell ${SQLITE_SOURCE_DIR}/shell.c)
    set_target_properties(sqlite3_shell PROPERTIES OUTPUT_NAME sqlite3)
    target_link_libraries(sqlite3_shell PRIVATE sqlite3)
    install(TARGETS sqlite3_shell)
endif()
