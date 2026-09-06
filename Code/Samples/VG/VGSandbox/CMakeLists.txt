# VG Sandbox - end-to-end VG stack demo (window + VGRenderer).
add_executable(VGSandbox Main.cpp)
target_link_libraries(VGSandbox PRIVATE
    Foundation::Core Foundation::RHI Foundation::Shaders Foundation::Shaders.System
    Samples::Framework
    Foundation::Image Foundation::Fonts Foundation::Fonts.TrueType Foundation::Fonts.DistanceField Foundation::Fonts.DistanceField.Baker
    Foundation::VG Foundation::VG.Renderer Foundation::VG.SVG)
target_compile_definitions(VGSandbox PRIVATE
    BUILTIN_VG_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
util_copy_sdl3_dll(VGSandbox)
