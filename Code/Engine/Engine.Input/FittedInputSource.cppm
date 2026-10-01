// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Input - :fitted_input partition (Sedulous f9b1feb7).
//
// The devices of another source with the MOUSE mapped into a fitted content space: a game drawn
// at a fixed render resolution and fitted into its window reads its pointer in render pixels,
// wherever the window puts the image. The keyboard, the pads, the touches and the event stream
// pass through.
//
// Ungated, unlike an editor viewport's surface: it is the whole window's input, so a player who
// never moves the mouse still has the keyboard. Over a letterbox bar the pointer maps outside the
// render area rather than vanishing.

module;
#include "Core/Prelude.h"

export module engine.input:fitted_input;

import foundation.core;
import foundation.shell;
import foundation.input;

using namespace foundation::core;

export namespace engine::input
{
    class FittedInputSource;

    /// The inner mouse, its position and motion in the fitted content space.
    class FittedMouse final : public foundation::shell::IMouse
    {
    public:
        explicit FittedMouse(FittedInputSource& source) noexcept : m_source(&source) {}

        [[nodiscard]] f32 X() const override;
        [[nodiscard]] f32 Y() const override;
        [[nodiscard]] f32 GlobalX() const override { return Raw().GlobalX(); }
        [[nodiscard]] f32 GlobalY() const override { return Raw().GlobalY(); }
        // Motion in render pixels too, so a look speed does not change with the window's size.
        [[nodiscard]] f32 DeltaX() const override;
        [[nodiscard]] f32 DeltaY() const override;
        [[nodiscard]] f32 ScrollX() const override { return Raw().ScrollX(); }
        [[nodiscard]] f32 ScrollY() const override { return Raw().ScrollY(); }
        [[nodiscard]] bool IsButtonDown(foundation::shell::MouseButton button) const override
        {
            return Raw().IsButtonDown(button);
        }
        [[nodiscard]] bool IsButtonPressed(foundation::shell::MouseButton button) const override
        {
            return Raw().IsButtonPressed(button);
        }
        [[nodiscard]] bool IsButtonReleased(foundation::shell::MouseButton button) const override
        {
            return Raw().IsButtonReleased(button);
        }
        [[nodiscard]] bool RelativeMode() const override { return Raw().RelativeMode(); }
        void SetRelativeMode(bool enabled) override { Raw().SetRelativeMode(enabled); }
        [[nodiscard]] bool CursorVisible() const override { return Raw().CursorVisible(); }
        void SetCursorVisible(bool visible) override { Raw().SetCursorVisible(visible); }
        void SetCursor(foundation::shell::CursorType cursor) override { Raw().SetCursor(cursor); }
        void SetGlobalCapture(bool enabled) override { Raw().SetGlobalCapture(enabled); }

    private:
        [[nodiscard]] foundation::shell::IMouse& Raw() const; // only reached while the inner has one
        FittedInputSource* m_source;
    };

    class FittedInputSource final : public foundation::input::IInputSourceProvider
    {
    public:
        /// `inner` is BORROWED: the source whose devices this presents.
        explicit FittedInputSource(foundation::input::IInputSourceProvider& inner) noexcept : m_inner(&inner) {}

        /// The window region (the region) and the render resolution (the content) with the fit.
        /// The owner keeps the region current as the window resizes.
        ContentFit fit{};

        [[nodiscard]] foundation::input::IInputSourceProvider& Inner() const noexcept { return *m_inner; }

        [[nodiscard]] foundation::shell::IKeyboard* Keyboard() override { return m_inner->Keyboard(); }
        [[nodiscard]] foundation::shell::IMouse* Mouse() override
        {
            return m_inner->Mouse() != nullptr ? &m_mouse : nullptr;
        }
        [[nodiscard]] i32 GamepadCount() const override { return m_inner->GamepadCount(); }
        [[nodiscard]] foundation::shell::IGamepad* Gamepad(i32 index) override { return m_inner->Gamepad(index); }
        [[nodiscard]] foundation::shell::ITouch* Touch() override { return m_inner->Touch(); }
        [[nodiscard]] Span<const foundation::shell::InputEvent> Events() override { return m_inner->Events(); }

        /// A window point in render pixels, through the fit and without the bars' clamp.
        [[nodiscard]] Float2 ToContent(Float2 point) const noexcept
        {
            const Rectangle dst = fit.DstRect();
            const Rectangle src = fit.SrcRect();
            if (dst.width <= 0.0f || dst.height <= 0.0f)
            {
                return point;
            }
            return Float2{src.x + (point.x - dst.x) / dst.width * src.width,
                          src.y + (point.y - dst.y) / dst.height * src.height};
        }

    private:
        foundation::input::IInputSourceProvider* m_inner;
        FittedMouse m_mouse{*this};
    };

    inline foundation::shell::IMouse& FittedMouse::Raw() const { return *m_source->Inner().Mouse(); }
    inline f32 FittedMouse::X() const { return m_source->ToContent(Float2{Raw().X(), Raw().Y()}).x; }
    inline f32 FittedMouse::Y() const { return m_source->ToContent(Float2{Raw().X(), Raw().Y()}).y; }
    inline f32 FittedMouse::DeltaX() const { return Raw().DeltaX() * m_source->fit.Scale().x; }
    inline f32 FittedMouse::DeltaY() const { return Raw().DeltaY() * m_source->fit.Scale().y; }
}
