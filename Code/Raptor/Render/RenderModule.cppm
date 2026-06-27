/// Raptor::Render — `raptor.render`, the renderer.
///
/// This phase is the scene -> render extraction bridge: render components (mesh /
/// camera) + their managers, and ExtractScene producing a renderer-agnostic draw list
/// (ExtractedView). Later partitions add the GPU forward path (mesh upload, PSO/bind
/// groups, draw recording) and the RenderSubsystem that injects the managers + drives
/// extraction + draw each frame.

export module raptor.render;

export import :components;
export import :view;
