# Draconic::Imgui - `draconic.imgui`, the Dear ImGui debug-UI integration (RHI renderer + subsystem).
# Links the vendored Draconic::ImGui PUBLIC so consumers (apps) get imgui.h + the ImGui:: API directly.
add_library(DraconicImgui STATIC)
add_library(Draconic::Imgui ALIAS DraconicImgui)

target_link_libraries(DraconicImgui
    PUBLIC Draconic::Core Draconic::RHI Draconic::Runtime Draconic::Shell
           Draconic::Graphics Draconic::Shaders Draconic::ShaderSystem
           Draconic::ImGui Draconic::Policy)

target_sources(DraconicImgui
    PUBLIC
        FILE_SET imgui_modules TYPE CXX_MODULES FILES
            ImguiRenderer.cppm
            ImguiSubsystem.cppm
            ImguiModule.cppm)
