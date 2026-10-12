// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Input - the fitted input source's out-of-line members (declared in :fitted_input): the
// key functions that keep the fitted devices' vtables in this module.

module;
#include "Core/Prelude.h"

module engine.input;

import foundation.core;
import foundation.shell;
import foundation.input;

using namespace foundation::core;

namespace engine::input
{
    foundation::shell::IMouse* FittedInputSource::Mouse()
    {
        return m_inner->Mouse() != nullptr ? &m_mouse : nullptr;
    }
    foundation::shell::ITouch* FittedInputSource::Touch()
    {
        return m_inner->Touch() != nullptr ? &m_touch : nullptr;
    }

    foundation::shell::IMouse& FittedMouse::Raw() const { return *m_source->Inner().Mouse(); }
    f32 FittedMouse::X() const { return m_source->ToContent(Float2{Raw().X(), Raw().Y()}).x; }
    f32 FittedMouse::Y() const { return m_source->ToContent(Float2{Raw().X(), Raw().Y()}).y; }
    // Turned with the content as well as scaled: on a turned present, moving the mouse up the
    // window moves right across the game.
    f32 FittedMouse::DeltaX() const
    {
        return m_source->fit.ToContentDelta(Float2{Raw().DeltaX(), Raw().DeltaY()}).x;
    }
    f32 FittedMouse::DeltaY() const
    {
        return m_source->fit.ToContentDelta(Float2{Raw().DeltaX(), Raw().DeltaY()}).y;
    }

    i32 FittedTouch::TouchCount() const
    {
        foundation::shell::ITouch* raw = m_source->Inner().Touch();
        return raw != nullptr ? raw->TouchCount() : 0;
    }
    bool FittedTouch::GetTouchPoint(i32 index, foundation::shell::TouchPoint& out) const
    {
        foundation::shell::ITouch* raw = m_source->Inner().Touch();
        foundation::shell::TouchPoint point;
        if (raw == nullptr || !raw->GetTouchPoint(index, point))
        {
            return false;
        }
        // Over a letterbox bar a finger maps outside the image (normalized below 0 or above 1)
        // rather than vanishing, as the mouse does.
        const Float2 content = m_source->ToContent(Float2{point.pointerX, point.pointerY});
        const Rectangle src = m_source->fit.SrcRect();
        out = point;
        out.pointerX = content.x;
        out.pointerY = content.y;
        out.x = src.width > 0.0f ? (content.x - src.x) / src.width : point.x;
        out.y = src.height > 0.0f ? (content.y - src.y) / src.height : point.y;
        return true;
    }
}
