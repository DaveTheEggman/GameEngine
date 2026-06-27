/// Raptor::Render — the `:data` partition.
///
/// The render data the renderer consumes — and the boundary that keeps the renderer
/// scene-agnostic. Render data is *extracted and pushed to* the renderer; the renderer
/// never reaches back into a scene (one-way dependency: the scene-integration layer in
/// raptor.render.subsystem depends on this, not the reverse). So `Renderable` carries
/// only what a draw needs (a world matrix + mesh + material + an opaque producer tag),
/// not an entity or a scene reference.

module;
#include "Core/Prelude.h"

export module raptor.render:data;

import raptor.core;
import raptor.geometry;
import raptor.materials;

using namespace raptor::core;

export namespace raptor::render {

// One thing to draw: a mesh + material at a world transform. Pointers are borrowed for
// the frame (the producer keeps the resources alive). `id` is an opaque tag the
// producer may set (e.g. a packed entity handle) for sorting/picking — meaningless to
// the renderer.
struct Renderable {
    Mat4                  worldMatrix = Mat4::Identity();
    geometry::StaticMesh* mesh        = nullptr;
    materials::Material*  material     = nullptr;
    u64                   id          = 0;
};

// A frame's worth of renderables + the camera that views them. This is the whole
// contract between "extract from the world" and "draw on the GPU".
struct ExtractedView {
    Mat4              view       = Mat4::Identity();
    Mat4              projection = Mat4::Identity();
    bool              hasCamera  = false;
    Array<Renderable> renderables;
};

} // namespace raptor::render
