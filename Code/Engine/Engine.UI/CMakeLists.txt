# Engine::UI - `engine.ui`: the game screen tier (canvases + overlay render + consumption).
add_library(Engine.UI STATIC)
add_library(Engine::UI ALIAS Engine.UI)
target_link_libraries(Engine.UI
    PUBLIC Foundation::Core Foundation::Runtime Foundation::Scene Engine::Scene
           Foundation::Resource Foundation::Shell Foundation::RHI Foundation::Fonts Foundation::Fonts.TTF Foundation::Fonts.Resource
           Foundation::Input Engine::Input Foundation::UI Foundation::UI.Shell
           Foundation::UI.Resource
           Foundation::Script Foundation::Script.Facades
           Foundation::VG Foundation::VG.Renderer Foundation::Shaders Foundation::Shaders.System
           Foundation::Render Engine::Render Foundation::Profiler Foundation::Policy)
target_sources(Engine.UI
    PRIVATE
        UISubsystemImpl.cpp   # implementation unit: VG/shader contact + reflection bodies
    PUBLIC
        FILE_SET ui_subsystem_modules TYPE CXX_MODULES FILES
            UISubsystem.cppm)

# Dev-mode shader source root: the ShaderSystemHost compiles the VG shaders (vg.vs/vg.ps) on demand
# from here when no cooked pack is present (a dist/browser ships shaders.dpak and needs no compiler).
target_compile_definitions(Engine.UI
    PRIVATE BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

