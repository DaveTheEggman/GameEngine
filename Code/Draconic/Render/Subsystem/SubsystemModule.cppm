/// Draconic::RenderSubsystem - `draconic.render.subsystem`, the scene side of rendering.
///
/// Render components (mesh / camera) + their managers, and the extraction that pushes a
/// render::ExtractedView to the renderer. This is where scene and renderer meet - it
/// depends on both draconic.scene and draconic.render; the renderer depends on neither. A
/// later partition adds the RenderSubsystem (injects the managers via ISceneAware, and
/// each frame extracts + draws).

export module draconic.render.subsystem;

export import :components;
export import :scene_renderer;
export import :extract;
export import :subsystem;
