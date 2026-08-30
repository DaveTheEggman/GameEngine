# GUI Sandbox - first on-screen test of experimental.gui (widgets + CSS on the VG renderer).
add_executable(GUISandbox Main.cpp)
target_link_libraries(GUISandbox PRIVATE
    Foundation::Core Foundation::RHI Foundation::Shaders Foundation::Shaders.System
    Samples::Framework
    Foundation::Shell Foundation::Image Foundation::Fonts Foundation::Fonts.TTF
    Foundation::VG Foundation::VG.Renderer Experimental::GUI Experimental::GUI.Shell)
target_compile_definitions(GUISandbox PRIVATE
    BUILTIN_GUI_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
util_copy_sdl3_dll(GUISandbox)
