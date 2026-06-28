/// Raptor::RenderSubsystem — the `:scene_renderer` partition.
///
/// `ISceneRenderer` — the scene-coupled frame coordinator the RenderSubsystem implements.
/// Its lifecycle mirrors the useful shape of Sedulous's ISceneRenderer:
///
///     BeginRendering(encoder, frameIndex);
///     RenderScene(sceneA, targetA, ...);
///     RenderScene(sceneB, targetB, ...);   // multiple scenes / views share frame state
///     EndRendering();
///
/// BeginRendering resets shared per-frame state (the view pool, the renderers' transient
/// buffers) once; each RenderScene extracts a scene into an immutable ExtractedScene and
/// collects a view over it; EndRendering composes every collected view into the frame. The
/// caller owns the encoder + targets + frame pacing. A `CameraOverride` supplies an explicit
/// camera (editor previews, reflection captures, multi-camera-of-one-scene) instead of the
/// scene's primary CameraComponent — the seam for secondary views (built out in phase 8).
///
/// NOTE: we deliberately adopt this Begin/RenderScene/End *lifecycle* but NOT Sedulous's
/// per-(scene, viewportKey) `Pipeline` objects — those are the god-objects our design (§9)
/// replaced with `RenderView` + the single `RenderFrame` driver + one frame graph.

module;
#include "Core/Prelude.h"

export module raptor.render.subsystem:scene_renderer;

import raptor.core;
import raptor.rhi;
import raptor.scene;
import raptor.render;   // ViewCamera

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// An explicit camera for a RenderScene call, bypassing the scene's primary CameraComponent.
struct CameraOverride {
    ViewCamera camera;
    Color      clearColor = Color{ 0.392f, 0.584f, 0.929f, 1.0f };   // the view's backdrop
};

// How the render target's resource state is handled. Default = the host-managed backbuffer (present).
// For an offscreen target, give its `texture` (so the graph barriers it) + the state it's currently
// in + the state to leave it in (ShaderRead to sample it next, CopySrc to blit it).
struct TargetState {
    rhi::Texture*      texture      = nullptr;
    rhi::ResourceState currentState = rhi::ResourceState::RenderTarget;
    rhi::ResourceState finalState   = rhi::ResourceState::RenderTarget;
};

// The scene-rendering coordinator. Implemented by RenderSubsystem; queried by the app via
// the Context (a renderer-agnostic seam for tools/editor that render scenes themselves).
class ISceneRenderer {
public:
    virtual ~ISceneRenderer() = default;

    // Begin a frame. Resets shared per-frame state. `encoder` (caller-owned) receives all
    // the frame's GPU commands; `frameIndex` is the device ring index.
    virtual void BeginRendering(rhi::CommandEncoder& encoder, u32 frameIndex) = 0;

    // Collect `scene`, viewed from its primary camera (or `cameraOverride` if given), to be
    // drawn into `target`. The view's clear color comes from that camera. Must be called
    // between Begin/EndRendering.
    // `viewport` is the sub-rect of `target` to render into (default = full target); pass distinct
    // viewports + camera overrides across multiple RenderScene calls for split-screen.
    virtual void RenderScene(scene::Scene& scene, rhi::TextureView* target, rhi::TextureFormat targetFormat,
                             u32 width, u32 height, ViewportRect viewport = {},
                             const CameraOverride* cameraOverride = nullptr,
                             const TargetState& targetState = {}) = 0;

    // Compose every collected view into the frame's encoder.
    virtual void EndRendering() = 0;
};

} // namespace raptor::render
