# Draconic::UISubsystem - `draconic.ui.subsystem`: the game screen tier (canvases + overlay render + consumption).
add_library(DraconicUISubsystem STATIC)
add_library(Draconic::UISubsystem ALIAS DraconicUISubsystem)
target_link_libraries(DraconicUISubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Resource Draconic::Shell Draconic::RHI Draconic::Fonts Draconic::FontsTTF
           Draconic::Input Draconic::InputSubsystem Draconic::UI Draconic::UIShell
           Draconic::UIResource
           Draconic::VG Draconic::VGRenderer Draconic::Shaders
           Draconic::Render Draconic::RenderSubsystem Draconic::Policy)
target_sources(DraconicUISubsystem
    PRIVATE
        UISubsystemImpl.cpp   # implementation unit: VG/shader contact + reflection bodies
    PUBLIC
        FILE_SET ui_subsystem_modules TYPE CXX_MODULES FILES
            UISubsystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
