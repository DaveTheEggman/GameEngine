/// Draconic::RenderApi - the `draconic.render.api` module.
///
/// The renderer's SCENE-RENDERING INTERFACE, extracted into a light module (core + rhi + scene
/// only) so tools can drive scene rendering through `ISceneRenderer` without linking the
/// renderer (the editor shell's layering rule; Sedulous models the same split with its
/// Abstractions assemblies). `draconic.render` re-exports everything here, so renderer-side
/// code is unaffected; `RenderSubsystem` implements `ISceneRenderer`.
///
/// The lifecycle (one bracket per frame, N views inside):
///
///     BeginRendering(encoder, frameIndex);
///     RenderScene(sceneA, targetA, ...);
///     RenderScene(sceneB, targetB, ...);   // multiple scenes / views share frame state
///     EndRendering();
///
/// BeginRendering resets shared per-frame state (the view pool, the renderers' transient
/// buffers) once; each RenderScene extracts a scene into an immutable ExtractedScene and
/// collects a view over it; EndRendering composes every collected view into the frame. The
/// caller owns the encoder + targets + frame pacing. Begin/EndRendering self-guard when the
/// renderer isn't ready yet, so a frame bracket may be driven unconditionally.

module;
#include "Core/Prelude.h"

export module draconic.render.api;

import draconic.core;
import draconic.rhi;
import draconic.scene;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// The camera for a view: world→view and view→clip, plus the world-space eye (for depth
// sorting / culling). The view matrix is the inverse of the camera entity's world matrix.
struct ViewCamera {
    Float4x4 view       = Float4x4::Identity();
    Float4x4 projection = Float4x4::Identity();
    Float3 position   = Float3{ 0, 0, 0 };
    f32  farZ       = 1000.0f;   // for depth-key normalization

    [[nodiscard]] Float4x4 ViewProjection() const noexcept { return view * projection; }
};

// A viewport sub-rect within a render target, in pixels. Width 0 => the full target.
struct ViewportRect {
    i32 x = 0, y = 0;
    u32 width = 0, height = 0;
};

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

} // namespace draconic::render
