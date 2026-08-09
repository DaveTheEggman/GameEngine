# ScriptPlayground - the foundation.script entity-behaviors P1 consumer proof: cubes driven
# by Wren Mover/Spinner behaviors ticked by the ScriptSubsystem under simulation.
add_executable(ScriptPlayground main.cpp)

target_link_libraries(ScriptPlayground PRIVATE
    Foundation::Core
    Foundation::Runtime
    Foundation::Runtime.Client
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Graphics
    Foundation::Graphics.Gpu
    Engine::DefaultApp
    Foundation::Scene
    Engine::Scene
    Engine::Render
    Foundation::Geometry Foundation::Geometry.Resource
    Foundation::Materials Foundation::Materials.Resource
    Foundation::Script Foundation::Script.Wren Foundation::Script.Resource
    Foundation::Script.Facades Engine::Script
    Foundation::Policy)
util_copy_sdl3_dll(ScriptPlayground)

target_compile_definitions(ScriptPlayground PRIVATE
    BUILTIN_PLAYGROUND_FONT="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
