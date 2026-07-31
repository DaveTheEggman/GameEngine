# Draconic::Engine.GameInstance - `draconic.engine.gameinstance`: a running game as a first-class
# object (scene pairing + script run context + `Game` + instance time scale). Over scene + script.
# See docs/design/game-instance.md.

add_library(Draconic.Engine.GameInstance STATIC)
add_library(Draconic::Engine.GameInstance ALIAS Draconic.Engine.GameInstance)

target_link_libraries(Draconic.Engine.GameInstance PUBLIC
    Draconic::Core Draconic::Scene Draconic::Script Draconic::Engine.Script
    Draconic::Net.Manager Draconic::Input Draconic::Policy)

target_sources(Draconic.Engine.GameInstance
    PUBLIC
        FILE_SET runtime_gameinstance_modules TYPE CXX_MODULES FILES
            GameInstance.cppm
    PRIVATE
        GameInstanceImpl.cpp)

# Tests link the AngelScript backend, which is not built for web (see DefaultApp) - desktop-only.
if(DRACONIC_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Engine.GameInstance.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
