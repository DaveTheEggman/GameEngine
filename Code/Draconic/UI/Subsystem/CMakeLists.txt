# Draconic::UISubsystem - `draconic.ui.subsystem`: the game screen tier (canvases + overlay render + consumption).
add_library(DraconicUISubsystem STATIC)
add_library(Draconic::UISubsystem ALIAS DraconicUISubsystem)
target_link_libraries(DraconicUISubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Resource Draconic::Shell Draconic::RHI Draconic::Fonts Draconic::FontsTTF
           Draconic::Input Draconic::InputSubsystem Draconic::UI Draconic::UIShell
           Draconic::UIResource
           Draconic::VG Draconic::VGRenderer Draconic::Shaders Draconic::ShaderSystem
           Draconic::Render Draconic::RenderSubsystem Draconic::Policy)
target_sources(DraconicUISubsystem
    PRIVATE
        UISubsystemImpl.cpp   # implementation unit: VG/shader contact + reflection bodies
    PUBLIC
        FILE_SET ui_subsystem_modules TYPE CXX_MODULES FILES
            UISubsystem.cppm)

# Dev-mode shader source root: the ShaderSystemHost compiles the VG shaders (vg.vs/vg.ps) on demand
# from here when no cooked pack is present (a dist/browser ships shaders.dpak and needs no compiler).
target_compile_definitions(DraconicUISubsystem
    PRIVATE DRACONIC_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
