# InputActions - the draconic.input consumer proof: named actions (WASD/stick move, Jump,
# an exclusive Menu set) driving visible state. No renderer - clear-color = position.
add_executable(InputActions main.cpp)

target_link_libraries(InputActions PRIVATE
    Draconic::Core
    Draconic::Runtime
    Draconic::RuntimeClient
    Draconic::ShellDesktop Draconic::RuntimeDesktop
    Draconic::Graphics
    Draconic::GraphicsGpu
    Draconic::Input Draconic::InputSubsystem
    Draconic::Settings Draconic::XmlSerialization Draconic::Imgui
    Draconic::Policy)
draconic_copy_sdl3_dll(InputActions)
