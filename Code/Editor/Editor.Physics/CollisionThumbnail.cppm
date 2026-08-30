// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor Physics - :collision_thumbnail partition.
//
// GPU thumbnail generator for collision shapes: the cooked product's outline triangles (the
// same soup the collider gizmos draw) rebuilt as a renderable mesh with flat facets, framed by
// its bounds. Data-only - the cooked outline is read as-is, never a physics world or Jolt
// shape. Registered from RegisterCollisionShapeEditor; the stage drives everything GPU.

module;
#include "Core/Prelude.h"

export module editor.physics:collision_thumbnail;

import foundation.core;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    /// Register the collision-shape thumbnail generator (the domain's composition entry
    /// point calls this; the count feeds the composition root's tripwire).
    void RegisterCollisionThumbnailGenerator(ThumbnailService& service);
}
