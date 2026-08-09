# Foundation::UI.Runtime - `draconic.ui.runtime`: the reusable UIHost that draws draconic.ui on the runtime's
# multi-window graphics host (one UIContext / N RootViews / per-window VGContext+VGRenderer+InputSurface).
# Toolkit-free - renders any RootView, shared by games and the editor. The docking/workbench layer lives
# above in draconic.ui.application.
add_library(UI.Runtime STATIC)
add_library(Foundation::UI.Runtime ALIAS UI.Runtime)

target_link_libraries(UI.Runtime
    PUBLIC
        Foundation::Core Foundation::RHI Foundation::Shaders Foundation::Shaders.System Foundation::Shell
        Foundation::Graphics Foundation::VG Foundation::VG.Renderer Foundation::Fonts Foundation::UI
        Foundation::UI.Shell Foundation::Policy)

target_sources(UI.Runtime
    PUBLIC
        FILE_SET uiruntime_modules TYPE CXX_MODULES FILES
            UIRuntime.cppm)

# Dev-mode shader source root: the ShaderSystemHost compiles the VG shaders (vg.vs/vg.ps) on demand
# from here when no cooked pack is present. A dist/browser ships shaders.dpak instead and needs none.
target_compile_definitions(UI.Runtime
    PRIVATE BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
