# UI Sandbox - on-screen test of draconic.ui (the Sedulous.UI port) on the VG renderer.
# Ports Sedulous's UISandbox demo (sans the Toolkit tabs) to prove the port end-to-end.
add_executable(UISandbox Main.cpp)
target_link_libraries(UISandbox PRIVATE
    Foundation::Core Foundation::RHI Foundation::Shaders
    Foundation::Runtime Foundation::Runtime.Client Foundation::Runtime.Desktop Foundation::Shell.Desktop
    Foundation::Graphics Foundation::Graphics.Gpu Foundation::UI.Runtime Foundation::UI.Application
    Foundation::Shell Foundation::Image Foundation::Fonts Foundation::Fonts.TTF
    Foundation::VG Foundation::VG.Renderer Foundation::UI Foundation::UI.Toolkit Foundation::UI.Viewport Foundation::UI.Shell Foundation::UI.VFS Foundation::VFS)
target_compile_definitions(UISandbox PRIVATE
    DRACONIC_UI_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    DRACONIC_UI_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets")
draconic_copy_sdl3_dll(UISandbox)
