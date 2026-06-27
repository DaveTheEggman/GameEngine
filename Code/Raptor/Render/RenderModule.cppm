/// Raptor::Render — `raptor.render`, the renderer (scene-agnostic).
///
/// The renderer consumes extracted render data (ExtractedView) and draws it on the
/// GPU; it knows nothing about the scene/ECS world. The scene-integration layer
/// (components, extraction, the RenderSubsystem) lives in raptor.render.subsystem and
/// depends on THIS — one-way. This phase is the render-data contract (:data); the GPU
/// forward path lands in a later partition.

export module raptor.render;

export import :data;
