# Extensions::Imgui - `draconic.imgui`, the Dear ImGui debug-UI integration (RHI renderer + subsystem).
# Links the vendored ThirdParty::ImGui PUBLIC so consumers (apps) get imgui.h + the ImGui:: API directly.
add_library(Imgui STATIC)
add_library(Extensions::Imgui ALIAS Imgui)

target_link_libraries(Imgui
    PUBLIC Foundation::Core Foundation::RHI Foundation::Runtime Foundation::Shell
           Foundation::Graphics Foundation::Shaders Foundation::Shaders.System
           ThirdParty::ImGui Foundation::Policy)

target_sources(Imgui
    PUBLIC
        FILE_SET imgui_modules TYPE CXX_MODULES FILES
            ImguiRenderer.cppm
            ImguiSubsystem.cppm
            ImguiModule.cppm)

# Dev-mode shader source root: the ShaderSystemHost compiles the ImGui shaders (imgui.vs/imgui.ps)
# on demand from here when no cooked pack is present (a dist/browser ships shaders.dpak instead).
target_compile_definitions(Imgui
    PRIVATE BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
