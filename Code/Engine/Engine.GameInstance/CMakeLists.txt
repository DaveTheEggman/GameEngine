# Engine::GameInstance - `engine.gameinstance`: a running game as a first-class
# object (scene pairing + script run context + `Game` + instance time scale). Over scene + script.
# See docs/design/game-instance.md.

add_library(Engine.GameInstance STATIC)
add_library(Engine::GameInstance ALIAS Engine.GameInstance)

target_link_libraries(Engine.GameInstance PUBLIC
    Foundation::Core Foundation::Scene Foundation::Scene.Resource Foundation::Content Foundation::Resource
    Foundation::Script Engine::Script Foundation::Script.Facades
    Foundation::Net.Manager Foundation::Input Foundation::Policy)

target_sources(Engine.GameInstance
    PUBLIC
        FILE_SET runtime_gameinstance_modules TYPE CXX_MODULES FILES
            NetworkController.cppm
            GameInstance.cppm
    PRIVATE
        GameInstanceImpl.cpp
        NetworkControllerImpl.cpp)

