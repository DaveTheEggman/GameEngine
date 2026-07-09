# GUI Sandbox - first on-screen test of draconic.gui (widgets + CSS on the VG renderer).
add_executable(GUISandbox Main.cpp)
target_link_libraries(GUISandbox PRIVATE
    Draconic::Core Draconic::RHI Draconic::RHIVulkan Draconic::Shaders Draconic::SamplesFramework
    Draconic::Shell Draconic::Image Draconic::Fonts Draconic::FontsTTF
    Draconic::VG Draconic::VGRenderer Draconic::GUI Draconic::GUIShell)
target_compile_definitions(GUISandbox PRIVATE
    DRACONIC_GUI_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
draconic_copy_sdl3_dll(GUISandbox)
