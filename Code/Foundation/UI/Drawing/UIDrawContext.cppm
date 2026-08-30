// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :draw_context partition
//
// Drawing context passed to View.OnDraw(). Wraps a VGContext with clip stacking, plus
// the font service and DPI scale. Ported from Sedulous.UI/src/Drawing/UIDrawContext.bf.
// Beef `VGContext mVG` (class = by ref) -> stored VGContext* (VG() returns the ref).

module;
#include "Core/Prelude.h"

export module foundation.ui:draw_context;

import foundation.core;  // Rectangle
import foundation.vg;    // VGContext
import foundation.fonts; // IFontService
import :debug_settings;

using namespace foundation::core;
namespace vg = foundation::vg;
namespace fonts = foundation::fonts;

export namespace foundation::ui
{
    class UIDrawContext
    {
    public:
        UIDrawContext(vg::VGContext& context, f32 dpiScale,
                      fonts::IFontService* fontService = nullptr,
                      UIDebugDrawSettings debugSettings = {}) noexcept
            : m_vg(&context), m_dpiScale(dpiScale), m_fontService(fontService),
              m_debugSettings(debugSettings)
        {
        }

        /// The underlying vector-graphics context.
        [[nodiscard]] vg::VGContext& VG() const noexcept { return *m_vg; }
        /// Current DPI scale.
        [[nodiscard]] f32 DpiScale() const noexcept { return m_dpiScale; }
        /// Font service for text rendering (may be null).
        [[nodiscard]] fonts::IFontService* FontService() const noexcept { return m_fontService; }
        /// Debug overlay settings.
        [[nodiscard]] const UIDebugDrawSettings& DebugSettings() const noexcept
        {
            return m_debugSettings;
        }

        /// Pushes a clip rectangle (in current local coordinates).
        void PushClip(const Rectangle& rect) { m_vg->PushClipRect(rect); }
        /// Pops the last pushed clip.
        void PopClip() { m_vg->PopClip(); }
        /// Whether a rect (current local coordinates) can contribute pixels under the active
        /// clip. True when no clip is active. The child-draw loop culls with this so clipped-out
        /// subtrees cost no tessellation (viewport-sized draw lists, not content-sized).
        [[nodiscard]] bool IsRectVisible(const Rectangle& rect) const
        {
            return m_vg->IsRectVisible(rect);
        }

    private:
        vg::VGContext* m_vg;
        f32 m_dpiScale;
        fonts::IFontService* m_fontService;
        UIDebugDrawSettings m_debugSettings;
    };
}
