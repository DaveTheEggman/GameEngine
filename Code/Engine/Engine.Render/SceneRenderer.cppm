// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Engine::Render - the `:scene_renderer` partition.
///
/// `ISceneRenderer` (+ CameraOverride/TargetState) MOVED to the light `foundation.render.api`
/// module so tools can drive scene rendering through the interface without linking the
/// renderer (the editor shell's layering rule). This partition re-exports it so subsystem
/// code and importers are unchanged; RenderSubsystem remains the implementation.

export module engine.render:scene_renderer;

export import foundation.render.api;
