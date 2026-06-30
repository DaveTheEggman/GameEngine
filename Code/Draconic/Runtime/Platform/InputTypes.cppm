// Draconic::RuntimePlatform — `:input_types` partition.
//
// Input-related enums and POD structs: keyboard codes/modifiers, mouse buttons
// and cursor types, gamepad buttons/axes, and touch points. Backend-neutral;
// the SDL3 backend maps native codes onto these. Ported from Draconic (itself a
// Sedulous port).

module;
#include "Core/Prelude.h"

export module draconic.runtime.platform:input_types;

import draconic.core;

namespace rc = draconic::core;

export namespace draconic::runtime
{
    // ---- Keyboard ---------------------------------------------------------

    enum class KeyCode : rc::u32
    {
        Unknown = 0,
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        Return, Escape, Backspace, Tab, Space,
        Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon,
        Apostrophe, Grave, Comma, Period, Slash,
        CapsLock, ScrollLock, NumLock,
        PrintScreen, Pause, Insert, Home, PageUp,
        Delete, End, PageDown,
        Right, Left, Down, Up,
        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
        Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus,
        KeypadEnter, KeypadDecimal,
        LeftCtrl, LeftShift, LeftAlt, LeftGui,
        RightCtrl, RightShift, RightAlt, RightGui,
        Menu,

        // Number of distinct codes; backends size their key arrays from this.
        Count,
    };

    enum class KeyModifiers : rc::u32
    {
        None       = 0,
        LeftShift  = 1 << 0,
        RightShift = 1 << 1,
        LeftCtrl   = 1 << 2,
        RightCtrl  = 1 << 3,
        LeftAlt    = 1 << 4,
        RightAlt   = 1 << 5,
        LeftGui    = 1 << 6,
        RightGui   = 1 << 7,
        NumLock    = 1 << 8,
        CapsLock   = 1 << 9,
        ScrollLock = 1 << 10,

        Shift = LeftShift | RightShift,
        Ctrl  = LeftCtrl  | RightCtrl,
        Alt   = LeftAlt   | RightAlt,
        Gui   = LeftGui   | RightGui,
    };

    inline constexpr KeyModifiers operator|(KeyModifiers a, KeyModifiers b) noexcept
    {
        return static_cast<KeyModifiers>(static_cast<rc::u32>(a) | static_cast<rc::u32>(b));
    }
    inline constexpr KeyModifiers operator&(KeyModifiers a, KeyModifiers b) noexcept
    {
        return static_cast<KeyModifiers>(static_cast<rc::u32>(a) & static_cast<rc::u32>(b));
    }
    inline constexpr KeyModifiers& operator|=(KeyModifiers& a, KeyModifiers b) noexcept
    {
        a = a | b;
        return a;
    }
    inline constexpr bool HasFlag(KeyModifiers mods, KeyModifiers flag) noexcept
    {
        return (mods & flag) == flag;
    }

    // ---- Mouse ------------------------------------------------------------

    enum class MouseButton : rc::u32
    {
        Left, Middle, Right, X1, X2,

        Count,
    };

    enum class CursorType : rc::u32
    {
        Default, Text, Wait, Crosshair, Progress,
        ResizeNWSE, ResizeNESW, ResizeEW, ResizeNS,
        ResizeNW, ResizeN, ResizeNE, ResizeE,
        ResizeSE, ResizeS, ResizeSW, ResizeW,
        Move, NotAllowed, Pointer,

        Count,
    };

    // ---- Gamepad ----------------------------------------------------------

    enum class GamepadButton : rc::u32
    {
        South, East, West, North,
        LeftShoulder, RightShoulder,
        LeftStick, RightStick,
        DPadUp, DPadDown, DPadLeft, DPadRight,
        Back, Guide, Start,
        LeftPaddle1, LeftPaddle2, RightPaddle1, RightPaddle2,
        Touchpad, Misc1,

        Count,
    };

    enum class GamepadAxis : rc::u32
    {
        LeftX, LeftY, RightX, RightY,
        LeftTrigger, RightTrigger,

        Count,
    };

    // ---- Touch ------------------------------------------------------------

    struct TouchPoint
    {
        rc::u64 id       = 0;
        rc::f32 x        = 0.0f;
        rc::f32 y        = 0.0f;
        rc::f32 pressure = 1.0f;
    };
}
