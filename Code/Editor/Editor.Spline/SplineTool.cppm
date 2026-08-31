// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Editor::Spline - `editor.spline`: the interactive spline viewport tool.
///
/// Control-point editing for SplineComponent on the IViewportTool framework: drag points on a
/// camera-facing plane, Ctrl+click a segment to insert at the closest curve position, Delete/X
/// removes the hovered point (two-point floor). Every gesture is one undo command (a
/// before/after snapshot of the point set, keyed by entity guid). Points live in ENTITY-LOCAL
/// space - the entity transform places the whole curve. Auto handles + the arc-length table
/// rebuild after every mutation, so consumers always see a coherent curve.

module;
#include "Core/Prelude.h"

export module editor.spline;

import foundation.core;
import editor.viewporttools;

using namespace foundation::core;

export namespace editor
{
    /// The provider contributing the spline tool to every scene viewport (explicit
    /// registration from Tools.Editor, never discovery).
    class SplineViewportToolProvider final : public IViewportToolProvider
    {
    public:
        void CreateTools(ViewportToolManager& manager,
                         const ViewportToolHostContext& context) override;
    };

    /// Register the spline viewport tool (call once at editor start, from Tools.Editor).
    void RegisterSplineViewportTools();
}
