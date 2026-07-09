# UI Sandbox - on-screen test of draconic.ui (the Sedulous.UI port) on the VG renderer.
# Ports Sedulous's UISandbox demo (sans the Toolkit tabs) to prove the port end-to-end.
add_executable(UISandbox Main.cpp)
target_link_libraries(UISandbox PRIVATE
    Draconic::Core Draconic::RHI Draconic::RHIVulkan Draconic::Shaders Draconic::SamplesFramework
    Draconic::Shell Draconic::Image Draconic::Fonts Draconic::FontsTTF
    Draconic::VG Draconic::VGRenderer Draconic::UI Draconic::UIShell Draconic::UIVFS)
target_compile_definitions(UISandbox PRIVATE
    DRACONIC_UI_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
draconic_copy_sdl3_dll(UISandbox)
