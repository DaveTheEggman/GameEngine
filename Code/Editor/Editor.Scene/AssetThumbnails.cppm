// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :asset_thumbnails partition.
//
// GPU thumbnail generators for the scene-domain assets. Meshes (the cooked product framed by
// its bounds, lit by a private sun, rendered with the neutral PBR default) and materials (the
// cooked material on a unit sphere) stage into the SHARED persistent scene through generator-
// owned entities toggled active per job. Prefabs (the instance spawned + bounds-framed),
// scene documents (loaded whole, viewed through their own primary camera when present), and
// particle effects (an emitter, sim-prewarmed by the stage) each take a PRIVATE per-job scene
// - arbitrary content never leaks into the shared stage. Registered from RegisterSceneEditor;
// the stage (editor.preview) drives Stage/Unstage and everything GPU.

module;
#include "Core/Prelude.h"

export module editor.scene:asset_thumbnails;

import foundation.core;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    /// Register the scene-domain thumbnail generators - mesh, material, prefab, scene
    /// document, particle effect (call from RegisterSceneEditor; the count feeds the
    /// composition root's tripwire; the context resolves source payloads per job).
    void RegisterSceneThumbnailGenerators(ThumbnailService& service, EditorContext& context);
}
