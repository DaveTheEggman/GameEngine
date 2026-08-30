// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - :bar partition
//
// Bar: a game fill bar over the CORE ProgressBar, adding an OPTIONAL SMOOTH DRAIN - AnimateTo tweens
// the fill toward a target over time on the CORE AnimationManager (the classic health/stamina drain),
// while the inherited Value still snaps instantly. Themeable through the ProgressBar `track`/`fill`
// style parts, so the GameTheme colours it.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.gamekit:bar;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    class Bar : public foundation::ui::ProgressBar
    {
        RTTI_OBJECT(Bar, foundation::ui::ProgressBar)
    public:
        Bar() = default;

        // Snap the fill to [0..1] (equivalent to setting the inherited Value directly).
        void SetFill(f32 value) { Value.SetValue(value); }

        // Smoothly animate the fill toward `target` over `duration` seconds on the CORE
        // AnimationManager (the drain). Headless (no context / animation manager) or duration<=0
        // snaps. A new call cancels this Bar's in-flight animation so drains never stack.
        void AnimateTo(f32 target, f32 duration);
        // Animate using the widget's default drain duration.
        void AnimateTo(f32 target) { AnimateTo(target, m_defaultDuration); }

        [[nodiscard]] f32 DefaultDuration() const noexcept { return m_defaultDuration; }
        void SetDefaultDuration(f32 seconds) noexcept
        {
            m_defaultDuration = seconds > 0.0f ? seconds : 0.0f;
        }

    private:
        f32 m_defaultDuration = 0.35f;
    };
}
