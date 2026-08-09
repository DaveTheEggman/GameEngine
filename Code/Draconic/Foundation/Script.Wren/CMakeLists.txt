# Foundation::Script.Wren - the Wren VM backend for Foundation::Script.
#
# Vendors the Wren VM (https://wren.io) via FetchContent and builds it as a C
# static lib, then the C++-module backend that implements IScriptManager /
# IScriptContext on top of it.

# Wren is C; the engine project only enables CXX by default.
enable_language(C)

include(FetchContent)
FetchContent_Declare(wren
    GIT_REPOSITORY https://github.com/wren-lang/wren.git
    GIT_TAG 0.4.0
    GIT_SHALLOW TRUE
    SYSTEM)
# Wren has no CMakeLists; MakeAvailable just populates and sets wren_SOURCE_DIR.
FetchContent_MakeAvailable(wren)

# Wren ships no CMakeLists; compile its VM + optional modules directly (C99).
file(GLOB WREN_SOURCES CONFIGURE_DEPENDS
    ${wren_SOURCE_DIR}/src/vm/*.c
    ${wren_SOURCE_DIR}/src/optional/*.c)

add_library(wren_vm STATIC ${WREN_SOURCES})
set_target_properties(wren_vm PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
# SYSTEM: third-party - keep our -Wall/-Werror off Wren's headers and sources.
target_include_directories(wren_vm SYSTEM
    PUBLIC ${wren_SOURCE_DIR}/src/include
    PRIVATE ${wren_SOURCE_DIR}/src/vm ${wren_SOURCE_DIR}/src/optional)

# The backend module library.
add_library(Script.Wren STATIC)
add_library(Foundation::Script.Wren ALIAS Script.Wren)

# wren_vm is PRIVATE: consumers see only the foundation.script.wren module (the
# factory), never the Wren C API. (CMake still links wren_vm into final exes.)
# Foundation::Script.Facades PUBLIC: WrenScript.cppm imports it for BehaviorFacadeNames()
# (the neutral facade-name list its behavior-module prelude is built from).
target_link_libraries(Script.Wren PUBLIC Foundation::Script Foundation::Script.Facades Foundation::Policy PRIVATE wren_vm)

target_sources(Script.Wren
    PUBLIC
        FILE_SET wren_modules TYPE CXX_MODULES FILES
            WrenScript.cppm)

