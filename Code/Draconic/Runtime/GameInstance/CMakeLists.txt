# Draconic::RuntimeGameInstance - `draconic.runtime.gameinstance`: a running game as a first-class
# object (scene pairing + script run context + `Game` + instance time scale). Over scene + script.
# See docs/design/game-instance.md.

add_library(DraconicRuntimeGameInstance STATIC)
add_library(Draconic::RuntimeGameInstance ALIAS DraconicRuntimeGameInstance)

target_link_libraries(DraconicRuntimeGameInstance PUBLIC
    Draconic::Core Draconic::Scene Draconic::Script Draconic::ScriptSubsystem Draconic::Policy)

target_sources(DraconicRuntimeGameInstance
    PUBLIC
        FILE_SET runtime_gameinstance_modules TYPE CXX_MODULES FILES
            GameInstance.cppm
    PRIVATE
        GameInstanceImpl.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
