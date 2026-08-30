// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - :ticker partition
//
// Ticker: a Label that shows an integer which ANIMATES to new values - the score/number that rolls up
// (or down) over a moment instead of snapping. AnimateTo tweens the displayed value on the CORE
// AnimationManager, rounding each frame; the exact target is written on completion (no float drift).
// It IS a Label, so alignment / font / colour all theme through the CORE Label surface.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.gamekit:ticker;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    class Ticker : public foundation::ui::Label
    {
        RTTI_OBJECT(Ticker, foundation::ui::Label)
    public:
        Ticker();

        // Snap the displayed number immediately.
        void SetNumber(i64 value);
        [[nodiscard]] i64 Number() const noexcept { return m_current; }

        // Roll the displayed number from its current value to `target` over `duration` seconds on the
        // CORE AnimationManager. Headless (no context) or duration<=0 snaps. A new call replaces the
        // in-flight roll. (Interpolation is f32, so exact mid-frames beyond ~16M lose precision; the
        // final value is always written exactly on completion.)
        void AnimateTo(i64 target, f32 duration);
        void AnimateTo(i64 target) { AnimateTo(target, m_defaultDuration); }

        [[nodiscard]] f32 DefaultDuration() const noexcept { return m_defaultDuration; }
        void SetDefaultDuration(f32 seconds) noexcept
        {
            m_defaultDuration = seconds > 0.0f ? seconds : 0.0f;
        }

    private:
        void Render(i64 value); // store + write the text

        i64 m_current = 0;
        f32 m_defaultDuration = 0.5f;
    };
}
