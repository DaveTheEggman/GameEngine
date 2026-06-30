# VG Sandbox — end-to-end VG stack demo (window + VGRenderer).
add_executable(VGSandbox Main.cpp)
target_link_libraries(VGSandbox PRIVATE
    Draconic::Core Draconic::RHI Draconic::RHIVulkan Draconic::Shaders Draconic::SamplesFramework
    Draconic::Image Draconic::Fonts Draconic::FontsTTF Draconic::VG Draconic::VGRenderer Draconic::VGSVG)
target_compile_definitions(VGSandbox PRIVATE
    DRACONIC_VG_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
draconic_copy_sdl3_dll(VGSandbox)
