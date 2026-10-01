// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::PropertyAnimation - the `editor.propertyanimation` primary module unit.
//
// Property animation is authored in the scene: the
// persistent PropertyAnimationPanel (`:panel`) docked below the viewport, over the shared
// ClipEditorView (`:clip_editor_view`). There is no standalone clip PAGE and no viewport tool mode.
// This unit is just the plugin registrar: it ensures the clip asset /
// source / resource types exist and contributes the "New Asset -> Property Animation Clip" creator.
// The scene page constructs and owns the panel directly.

module;
#include "Core/Prelude.h"

export module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.runtime;
import foundation.runtime.client; // IApplicationHost (the registrar signature)
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import propertyanimation.pipeline;
import editor.core;

export import :clip_editor_view;
export import :panel;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;

    /// The editor executable's entry point for the property-animation plugin. `host` is currently
    /// unused (the panel is scene-page-owned, not a globally registered page), kept for a uniform
    /// registrar signature across the editor plugins.
    inline void RegisterPropertyAnimationEditor(EditorContext& context, runtime::IApplicationHost& host)
    {
        (void)host;
        (void)context;
        pipeline::RegisterPropertyAnimationAssets(); // ensure the asset/source/resource types exist
    }
}
