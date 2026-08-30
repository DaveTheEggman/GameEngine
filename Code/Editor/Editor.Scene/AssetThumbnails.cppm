// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :asset_thumbnails partition.
//
// GPU thumbnail generators for the scene-domain assets: meshes (the cooked product framed by
// its bounds, lit by a private sun, rendered with the neutral PBR default) and materials (the
// cooked material on a unit sphere). Each generator owns PERSISTENT entities in the shared
// stage scene - created once, toggled active per job - so a job costs a resource bind and a
// transform, never scene setup. Registered from RegisterSceneEditor; the stage (editor.preview)
// drives Stage/Unstage and everything GPU.

module;
#include "Core/Prelude.h"

export module editor.scene:asset_thumbnails;

import foundation.core;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    /// Register the mesh + material scene-thumbnail generators (call from RegisterSceneEditor;
    /// the count feeds the composition root's tripwire).
    void RegisterSceneThumbnailGenerators(ThumbnailService& service);
}
