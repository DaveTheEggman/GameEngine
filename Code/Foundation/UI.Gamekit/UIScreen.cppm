// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - :screen partition
//
// UIScreen: one game screen/page (main menu, pause, HUD, game-over). A ViewGroup that fills its parent
// (the screen tier's RootView) and is managed by a ScreenStack. Authored as a UIDocument with a
// `<screen mode=.. transition=.. default-focus=..>` root element (registered with the markup system by
// RegisterGamekitMarkup), or wrapped around a plain document root by the stack.
//
// Three INPUT MODES decide what happens to screens below when this one is on top:
//   - Overlay: pass-through (empty areas do not eat input) and lower screens stay visible - the HUD.
//   - Modal:   this screen shields input from everything below (a full-surface hit test) but lower
//              screens stay visible behind it - a pause menu over a frozen game.
//   - Opaque:  shields input AND hides lower screens entirely - a full-screen menu.
//
// LIFECYCLE hooks (called by the ScreenStack, not by CORE attach): OnEnter (pushed), OnExit (popped),
// OnShown (became the top / uncovered), OnHidden (covered by a newer screen). Default no-ops; native
// subclasses or the script tier can react.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.gamekit:screen;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    /// How a screen treats input + visibility of screens below it while it is on top of the stack.
    enum class ScreenMode : u8
    {
        Overlay = 0, // pass-through input; lower screens visible (HUD)
        Modal,       // shield input below; lower screens visible (pause menu)
        Opaque       // shield input below; lower screens hidden (full-screen menu)
    };

    /// A screen's declared enter/exit animation. Played by the ScreenStack on the CORE Animation layer.
    enum class TransitionKind : u8
    {
        None = 0,
        Fade,
        SlideLeft,  // enters from / exits to the right edge moving left
        SlideRight, // enters from / exits to the left edge moving right
        SlideUp,
        SlideDown,
        Scale // scale + fade from 0.9 -> 1.0
    };

    struct TransitionDesc
    {
        TransitionKind kind = TransitionKind::None;
        f32 duration = 0.2f;
        core::EasingFunction easing = nullptr; // nullptr -> the stack's default ease
    };

    /// A page/screen view managed by a ScreenStack. Fills its parent; children position via their own
    /// layout params / alignment (like a Panel that always expands to the available box).
    class UIScreen : public foundation::ui::ViewGroup
    {
        RTTI_OBJECT(UIScreen, foundation::ui::ViewGroup)
    public:
        UIScreen() = default;

        // --- authored configuration ---
        [[nodiscard]] ScreenMode Mode() const noexcept { return m_mode; }
        void SetMode(ScreenMode mode) noexcept { m_mode = mode; }

        [[nodiscard]] const TransitionDesc& InTransition() const noexcept { return m_in; }
        [[nodiscard]] const TransitionDesc& OutTransition() const noexcept { return m_out; }
        void SetInTransition(TransitionDesc t) noexcept { m_in = t; }
        void SetOutTransition(TransitionDesc t) noexcept { m_out = t; }
        // A screen usually enters and exits with the same effect; SetTransition sets both (exit reversed
        // by the stack) - the common authoring case.
        void SetTransition(TransitionDesc t) noexcept
        {
            m_in = t;
            m_out = t;
        }

        // Name of the child focused when this screen becomes active (so a pad/keyboard user lands on
        // something). Empty -> the stack focuses the first focusable descendant.
        [[nodiscard]] StringView DefaultFocus() const noexcept { return m_defaultFocus.AsView(); }
        void SetDefaultFocus(StringView name) { m_defaultFocus = String(name); }

        // Whether this screen blocks input from reaching screens below it (Modal + Opaque do).
        [[nodiscard]] bool ShieldsInput() const noexcept { return m_mode != ScreenMode::Overlay; }
        // Whether this screen fully hides screens below it (Opaque does).
        [[nodiscard]] bool HidesBelow() const noexcept { return m_mode == ScreenMode::Opaque; }

        // --- lifecycle (invoked by the ScreenStack) ---
        virtual void OnEnter() {}  // pushed onto the stack
        virtual void OnExit() {}   // popped off the stack
        virtual void OnShown() {}  // became the top (pushed, or uncovered by a pop above)
        virtual void OnHidden() {} // covered by a newer screen pushed on top

        // --- string parsers for markup attributes (RegisterGamekitMarkup wires these) ---
        void SetModeFromString(StringView value);
        void SetInTransitionFromString(StringView value);
        void SetOutTransitionFromString(StringView value);
        void SetTransitionFromString(StringView value); // sets both

    protected:
        void OnDraw(foundation::ui::UIDrawContext& ctx) override { DrawChildren(ctx); }

        void OnMeasure(foundation::ui::BoxConstraints constraints) override
        {
            const Thickness pad = ResolveBoxMetrics().Chrome();
            const foundation::ui::BoxConstraints inner = constraints.Deflate(pad).Loosen();
            f32 maxW = 0.0f;
            f32 maxH = 0.0f;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == foundation::ui::Visibility::Gone)
                {
                    continue;
                }
                child->Measure(inner);
                const Float2 mb = child->MarginBoxSize();
                maxW = Max(maxW, mb.x);
                maxH = Max(maxH, mb.y);
            }
            // A screen FILLS the tier: take the available box when the parent is bounded (the usual
            // case under a RootView), else fall back to the children extent.
            MeasuredSize = Float2{constraints.BoundedMaxWidth(maxW + pad.TotalHorizontal()),
                                  constraints.BoundedMaxHeight(maxH + pad.TotalVertical())};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            const Thickness pad = ResolveBoxMetrics().Chrome();
            const f32 contentW = width - pad.TotalHorizontal();
            const f32 contentH = height - pad.TotalVertical();
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == foundation::ui::Visibility::Gone)
                {
                    continue;
                }
                child->Layout(pad.Left, pad.Top, Max(0.0f, contentW), Max(0.0f, contentH));
            }
        }

    private:
        ScreenMode m_mode = ScreenMode::Modal;
        TransitionDesc m_in{};
        TransitionDesc m_out{};
        String m_defaultFocus;
    };

    // Register the `<screen>` element + its attributes (mode / transition / in-transition /
    // out-transition / default-focus) with the CORE markup registry so authored .sml documents whose
    // root is `<screen ...>` instantiate a UIScreen. Idempotent; call once at startup (the screen host
    // does). Also safe to skip: the ScreenStack wraps a non-screen document root in a default UIScreen.
    void RegisterGamekitMarkup();
}
