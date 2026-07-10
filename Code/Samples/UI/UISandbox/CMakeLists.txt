# UI Sandbox - on-screen test of draconic.ui (the Sedulous.UI port) on the VG renderer.
# Ports Sedulous's UISandbox demo (sans the Toolkit tabs) to prove the port end-to-end.
add_executable(UISandbox Main.cpp)
target_link_libraries(UISandbox PRIVATE
    Draconic::Core Draconic::RHI Draconic::Shaders
    Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDesktop Draconic::ShellDesktop
    Draconic::Graphics Draconic::GraphicsGpu Draconic::UIRuntime Draconic::UIApplication
    Draconic::Shell Draconic::Image Draconic::Fonts Draconic::FontsTTF
    Draconic::VG Draconic::VGRenderer Draconic::UI Draconic::UIToolkit Draconic::UIShell Draconic::UIVFS Draconic::VFS)
target_compile_definitions(UISandbox PRIVATE
    DRACONIC_UI_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    DRACONIC_UI_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets")
draconic_copy_sdl3_dll(UISandbox)
