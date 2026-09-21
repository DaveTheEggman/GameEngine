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

    /// The spline tool for one host (what the provider adds to a viewport's manager), for a
    /// host that drives it directly - a headless test.
    [[nodiscard]] UniquePtr<IViewportTool> CreateSplineEditTool(const ViewportToolHostContext& context);

    /// What the spline tool is doing, read off its Id-checked instance: the hovered and
    /// selected point (-1 = none), whether a drag is live, whether Ctrl shows an insert
    /// preview. All defaults for a tool that is not the spline tool.
    struct SplineEditToolState
    {
        i32 hoverPoint = -1;
        i32 selectedPoint = -1;
        bool dragging = false;
        bool insertPreview = false;
        bool placePreview = false; // Ctrl over a curve with no segment: a click appends here
    };
    [[nodiscard]] SplineEditToolState SplineEditToolStateOf(const IViewportTool& tool);
}
