// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// editor.vegetation - the vegetation editing module's public face: the Paint Vegetation viewport
// brush, its viewport-tool provider and the panel registrar.

export module editor.vegetation;

export import :pick;    // VegetationPick: the terrain footprint under the cursor (both brushes)
export import :paint;   // VegetationPaintTool + RegisterVegetationViewportTools / ToolPanels
export import :scatter; // VegetationScatterTool (Paint Props)
