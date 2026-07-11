/// Draconic::RenderSubsystem - the `:scene_renderer` partition.
///
/// `ISceneRenderer` (+ CameraOverride/TargetState) MOVED to the light `draconic.render.api`
/// module so tools can drive scene rendering through the interface without linking the
/// renderer (the editor shell's layering rule). This partition re-exports it so subsystem
/// code and importers are unchanged; RenderSubsystem remains the implementation.

export module draconic.render.subsystem:scene_renderer;

export import draconic.render.api;
