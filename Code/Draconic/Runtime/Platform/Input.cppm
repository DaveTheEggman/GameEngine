// Draconic::RuntimePlatform — `:input` partition.
//
// Abstract input device interfaces: IKeyboard, IMouse, IGamepad, ITouch, and
// IInputManager (the aggregate the platform exposes). State is double-buffered
// per frame so callers can ask Down (held), Pressed (this frame), and Released
// (this frame). Backends implement these; IInputManager::Update() rolls the
// frame. Ported from Draconic (itself a Sedulous port).

module;
#include "Core/Prelude.h"

export module draconic.runtime.platform:input;

import draconic.core;
import :input_types;

namespace rc = draconic::core;

export namespace draconic::runtime
{
    class IKeyboard
    {
    public:
        virtual ~IKeyboard() = default;

        [[nodiscard]] virtual bool IsKeyDown(KeyCode key)     const = 0;  // held
        [[nodiscard]] virtual bool IsKeyPressed(KeyCode key)  const = 0;  // went down this frame
        [[nodiscard]] virtual bool IsKeyReleased(KeyCode key) const = 0;  // went up this frame
        [[nodiscard]] virtual KeyModifiers Modifiers()        const = 0;
    };

    class IMouse
    {
    public:
        virtual ~IMouse() = default;

        [[nodiscard]] virtual rc::f32 X()       const = 0;  // window-space position
        [[nodiscard]] virtual rc::f32 Y()       const = 0;
        [[nodiscard]] virtual rc::f32 DeltaX()  const = 0;  // movement this frame
        [[nodiscard]] virtual rc::f32 DeltaY()  const = 0;
        [[nodiscard]] virtual rc::f32 ScrollX() const = 0;  // wheel this frame
        [[nodiscard]] virtual rc::f32 ScrollY() const = 0;

        [[nodiscard]] virtual bool IsButtonDown(MouseButton button)     const = 0;
        [[nodiscard]] virtual bool IsButtonPressed(MouseButton button)  const = 0;
        [[nodiscard]] virtual bool IsButtonReleased(MouseButton button) const = 0;

        [[nodiscard]] virtual bool RelativeMode() const = 0;
        virtual void SetRelativeMode(bool enabled) = 0;
        [[nodiscard]] virtual bool CursorVisible() const = 0;
        virtual void SetCursorVisible(bool visible) = 0;
        virtual void SetCursor(CursorType cursor) = 0;
    };

    class IGamepad
    {
    public:
        virtual ~IGamepad() = default;

        [[nodiscard]] virtual rc::i32        Index()     const = 0;
        [[nodiscard]] virtual rc::StringView Name()      const = 0;
        [[nodiscard]] virtual bool           Connected() const = 0;

        [[nodiscard]] virtual bool IsButtonDown(GamepadButton button)     const = 0;
        [[nodiscard]] virtual bool IsButtonPressed(GamepadButton button)  const = 0;
        [[nodiscard]] virtual bool IsButtonReleased(GamepadButton button) const = 0;
        [[nodiscard]] virtual rc::f32 Axis(GamepadAxis axis)              const = 0;  // [-1,1], triggers [0,1]

        // Low/high-frequency motor strengths in [0,1] for durationMs milliseconds.
        virtual void SetRumble(rc::f32 lowFreq, rc::f32 highFreq, rc::u32 durationMs) = 0;
    };

    class ITouch
    {
    public:
        virtual ~ITouch() = default;

        [[nodiscard]] virtual rc::i32 TouchCount() const = 0;
        [[nodiscard]] virtual bool GetTouchPoint(rc::i32 index, TouchPoint& out) const = 0;
        [[nodiscard]] virtual bool HasTouch() const = 0;
    };

    // Aggregate exposed by IPlatform::Input(). Devices are owned by the manager.
    class IInputManager
    {
    public:
        virtual ~IInputManager() = default;

        [[nodiscard]] virtual IKeyboard* Keyboard() = 0;
        [[nodiscard]] virtual IMouse*    Mouse()    = 0;
        [[nodiscard]] virtual ITouch*    Touch()    = 0;
        [[nodiscard]] virtual rc::i32    GamepadCount() const = 0;
        [[nodiscard]] virtual IGamepad*  GetGamepad(rc::i32 index) = 0;

        // Rolls per-frame state (current -> previous, clears deltas). The
        // platform calls this once per frame before pumping OS events.
        virtual void Update() = 0;
    };
}
