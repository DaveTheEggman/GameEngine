/// Raptor::Render — `raptor.render`, the renderer (scene-agnostic).
///
/// The renderer consumes a per-scene `ExtractedScene` (world-space `RenderData`) and draws
/// the views over it; it knows nothing about the scene/ECS world. The scene-integration
/// layer (components, extraction, the RenderSubsystem) lives in raptor.render.subsystem and
/// depends on THIS — one-way.
///
/// Partitions: `:data` (the render-data contract + frame arena + radix sort), `:views`
/// (RenderView isolation boundary + pool), `:pipeline` (the Renderer/Pass extension seam +
/// the single per-frame RenderFrame driver), `:mesh_renderer` (the built-in mesh drawer),
/// `:gpu_mesh` (mesh GPU upload cache).

export module raptor.render;

export import :data;
export import :views;
export import :extract_ctx;
export import :resources;
export import :pipeline;
export import :gpu_mesh;
export import :mesh_renderer;
export import :cluster_system;
export import :tonemap;
