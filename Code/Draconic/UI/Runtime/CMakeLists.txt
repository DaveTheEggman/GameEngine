# Draconic::UIRuntime - `draconic.ui.runtime`: the reusable UIHost that draws draconic.ui on the runtime's
# multi-window graphics host (one UIContext / N RootViews / per-window VGContext+VGRenderer+InputSurface).
# Toolkit-free - renders any RootView, shared by games and the editor. The docking/workbench layer lives
# above in draconic.ui.application.
add_library(DraconicUIRuntime STATIC)
add_library(Draconic::UIRuntime ALIAS DraconicUIRuntime)

target_link_libraries(DraconicUIRuntime
    PUBLIC
        Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem Draconic::Shell
        Draconic::Graphics Draconic::VG Draconic::VGRenderer Draconic::Fonts Draconic::UI
        Draconic::UIShell Draconic::Policy)

target_sources(DraconicUIRuntime
    PUBLIC
        FILE_SET uiruntime_modules TYPE CXX_MODULES FILES
            UIRuntime.cppm)

# Dev-mode shader source root: the ShaderSystemHost compiles the VG shaders (vg.vs/vg.ps) on demand
# from here when no cooked pack is present. A dist/browser ships shaders.dpak instead and needs none.
target_compile_definitions(DraconicUIRuntime
    PRIVATE DRACONIC_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
