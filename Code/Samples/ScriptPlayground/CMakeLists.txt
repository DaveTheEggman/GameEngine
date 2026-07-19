# ScriptPlayground - the draconic.script entity-behaviors P1 consumer proof: cubes driven
# by Wren Mover/Spinner behaviors ticked by the ScriptSubsystem under simulation.
add_executable(ScriptPlayground main.cpp)

target_link_libraries(ScriptPlayground PRIVATE
    Draconic::Core
    Draconic::Runtime
    Draconic::RuntimeClient
    Draconic::ShellDesktop Draconic::RuntimeDesktop
    Draconic::Graphics
    Draconic::GraphicsGpu
    Draconic::RuntimeDefaultApp
    Draconic::Scene
    Draconic::SceneSubsystem
    Draconic::RenderSubsystem
    Draconic::Geometry Draconic::MeshResource
    Draconic::Materials Draconic::MaterialResource
    Draconic::Script Draconic::ScriptWren Draconic::ScriptResource
    Draconic::ScriptFacades Draconic::ScriptSubsystem
    Draconic::Policy)
draconic_copy_sdl3_dll(ScriptPlayground)

target_compile_definitions(ScriptPlayground PRIVATE
    DRACONIC_PLAYGROUND_FONT="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
