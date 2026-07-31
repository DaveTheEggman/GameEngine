// Draconic::ShellWeb - `draconic.shell.web:input`.
//
// The web shell's input devices, wired to the browser via Emscripten's HTML5 event callbacks
// (keydown/up on the window, mouse move/down/up + wheel on the canvas). Those callbacks fire
// ASYNCHRONOUSLY between animation frames, so they ENQUEUE raw events; WebInputManager::Update()
// (called once per frame from the shell's ProcessEvents) drains the queue AFTER snapshotting the
// previous frame's state - which keeps IsKeyPressed/Released ("went down/up THIS frame") correct,
// the same model the SDL3 desktop shell uses (BeginFrame, then apply the frame's events).

module;
#include "Core/Prelude.h"
#include <emscripten/html5.h>

export module draconic.shell.web:input;

import draconic.core;
import draconic.shell;

namespace core = draconic::core;

export namespace draconic::shell
{
    // Map a DOM KeyboardEvent.code (the physical key, layout-independent - "KeyW", "ArrowUp",
    // "Space", "ShiftLeft", ...) to a Draconic KeyCode.
    [[nodiscard]] inline KeyCode KeyCodeFromDom(const char* code) noexcept
    {
        if (code == nullptr || code[0] == '\0')
        {
            return KeyCode::Unknown;
        }
        const core::StringView c(reinterpret_cast<const core::utf8char*>(code));
        const auto eq = [&c](const char8_t* s) { return c == core::StringView(s); };

        // Letters: "KeyA".."KeyZ".
        if (c.Size() == 4 && code[0] == 'K' && code[1] == 'e' && code[2] == 'y')
        {
            const char ch = code[3];
            if (ch >= 'A' && ch <= 'Z')
            {
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::A) + (ch - 'A'));
            }
        }
        // Digits: "Digit0".."Digit9".
        if (c.Size() == 6 && code[5] >= '0' && code[5] <= '9' &&
            core::StringView(reinterpret_cast<const core::utf8char*>("Digit")) == c.SubStr(0, 5))
        {
            return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::Num0) + (code[5] - '0'));
        }
        // Numpad digits: "Numpad0".."Numpad9".
        if (c.Size() == 7 && core::StringView(reinterpret_cast<const core::utf8char*>("Numpad")) ==
                                 c.SubStr(0, 6) &&
            code[6] >= '0' && code[6] <= '9')
        {
            return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::Keypad0) + (code[6] - '0'));
        }
        // Function keys "F1".."F24".
        if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9')
        {
            core::i32 n = code[1] - '0';
            if (code[2] >= '0' && code[2] <= '9')
            {
                n = n * 10 + (code[2] - '0');
            }
            if (n >= 1 && n <= 24)
            {
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::F1) + (n - 1));
            }
        }

        // Named keys.
        if (eq(u8"Space")) return KeyCode::Space;
        if (eq(u8"Enter")) return KeyCode::Return;
        if (eq(u8"Escape")) return KeyCode::Escape;
        if (eq(u8"Backspace")) return KeyCode::Backspace;
        if (eq(u8"Tab")) return KeyCode::Tab;
        if (eq(u8"Minus")) return KeyCode::Minus;
        if (eq(u8"Equal")) return KeyCode::Equals;
        if (eq(u8"BracketLeft")) return KeyCode::LeftBracket;
        if (eq(u8"BracketRight")) return KeyCode::RightBracket;
        if (eq(u8"Backslash")) return KeyCode::Backslash;
        if (eq(u8"Semicolon")) return KeyCode::Semicolon;
        if (eq(u8"Quote")) return KeyCode::Apostrophe;
        if (eq(u8"Backquote")) return KeyCode::Grave;
        if (eq(u8"Comma")) return KeyCode::Comma;
        if (eq(u8"Period")) return KeyCode::Period;
        if (eq(u8"Slash")) return KeyCode::Slash;
        if (eq(u8"CapsLock")) return KeyCode::CapsLock;
        if (eq(u8"ArrowRight")) return KeyCode::Right;
        if (eq(u8"ArrowLeft")) return KeyCode::Left;
        if (eq(u8"ArrowDown")) return KeyCode::Down;
        if (eq(u8"ArrowUp")) return KeyCode::Up;
        if (eq(u8"Insert")) return KeyCode::Insert;
        if (eq(u8"Home")) return KeyCode::Home;
        if (eq(u8"PageUp")) return KeyCode::PageUp;
        if (eq(u8"Delete")) return KeyCode::Delete;
        if (eq(u8"End")) return KeyCode::End;
        if (eq(u8"PageDown")) return KeyCode::PageDown;
        if (eq(u8"ControlLeft")) return KeyCode::LeftCtrl;
        if (eq(u8"ShiftLeft")) return KeyCode::LeftShift;
        if (eq(u8"AltLeft")) return KeyCode::LeftAlt;
        if (eq(u8"MetaLeft")) return KeyCode::LeftGui;
        if (eq(u8"ControlRight")) return KeyCode::RightCtrl;
        if (eq(u8"ShiftRight")) return KeyCode::RightShift;
        if (eq(u8"AltRight")) return KeyCode::RightAlt;
        if (eq(u8"MetaRight")) return KeyCode::RightGui;
        if (eq(u8"ContextMenu")) return KeyCode::Menu;
        if (eq(u8"NumpadEnter")) return KeyCode::KeypadEnter;
        if (eq(u8"NumpadAdd")) return KeyCode::KeypadPlus;
        if (eq(u8"NumpadSubtract")) return KeyCode::KeypadMinus;
        if (eq(u8"NumpadMultiply")) return KeyCode::KeypadMultiply;
        if (eq(u8"NumpadDivide")) return KeyCode::KeypadDivide;
        if (eq(u8"NumpadDecimal")) return KeyCode::KeypadDecimal;
        return KeyCode::Unknown;
    }

    class WebKeyboard final : public IKeyboard
    {
    public:
        [[nodiscard]] bool IsKeyDown(KeyCode k) const override { return m_current[Index(k)]; }
        [[nodiscard]] bool IsKeyPressed(KeyCode k) const override
        {
            return m_current[Index(k)] && !m_previous[Index(k)];
        }
        [[nodiscard]] bool IsKeyReleased(KeyCode k) const override
        {
            return !m_current[Index(k)] && m_previous[Index(k)];
        }
        [[nodiscard]] KeyModifiers Modifiers() const override { return m_mods; }

        void SetKey(KeyCode k, bool down) { m_current[Index(k)] = down; }
        void SetModifiers(KeyModifiers m) { m_mods = m; }
        void BeginFrame()
        {
            for (core::u32 i = 0; i < kCount; ++i)
            {
                m_previous[i] = m_current[i];
            }
        }

    private:
        static constexpr core::u32 kCount = static_cast<core::u32>(KeyCode::Count);
        static core::u32 Index(KeyCode k) noexcept
        {
            const core::u32 i = static_cast<core::u32>(k);
            return i < kCount ? i : 0;
        }
        bool m_current[kCount] = {};
        bool m_previous[kCount] = {};
        KeyModifiers m_mods = KeyModifiers::None;
    };

    class WebMouse final : public IMouse
    {
    public:
        [[nodiscard]] core::f32 X() const override { return m_x; }
        [[nodiscard]] core::f32 Y() const override { return m_y; }
        [[nodiscard]] core::f32 GlobalX() const override { return m_x; }
        [[nodiscard]] core::f32 GlobalY() const override { return m_y; }
        [[nodiscard]] core::f32 DeltaX() const override { return m_dx; }
        [[nodiscard]] core::f32 DeltaY() const override { return m_dy; }
        [[nodiscard]] core::f32 ScrollX() const override { return m_sx; }
        [[nodiscard]] core::f32 ScrollY() const override { return m_sy; }
        [[nodiscard]] bool IsButtonDown(MouseButton b) const override { return m_current[Index(b)]; }
        [[nodiscard]] bool IsButtonPressed(MouseButton b) const override
        {
            return m_current[Index(b)] && !m_previous[Index(b)];
        }
        [[nodiscard]] bool IsButtonReleased(MouseButton b) const override
        {
            return !m_current[Index(b)] && m_previous[Index(b)];
        }
        [[nodiscard]] bool RelativeMode() const override { return m_relative; }
        void SetRelativeMode(bool enabled) override { m_relative = enabled; } // pointer-lock: later
        [[nodiscard]] bool CursorVisible() const override { return m_cursorVisible; }
        void SetCursorVisible(bool v) override { m_cursorVisible = v; }
        void SetCursor(CursorType) override {}
        void SetGlobalCapture(bool) override {}

        void OnMotion(core::f32 x, core::f32 y, core::f32 dx, core::f32 dy)
        {
            m_x = x;
            m_y = y;
            m_dx += dx;
            m_dy += dy;
        }
        void OnButton(core::u32 button, bool down)
        {
            if (button < kCount)
            {
                m_current[button] = down;
            }
        }
        void OnWheel(core::f32 sx, core::f32 sy)
        {
            m_sx += sx;
            m_sy += sy;
        }
        void BeginFrame()
        {
            for (core::u32 i = 0; i < kCount; ++i)
            {
                m_previous[i] = m_current[i];
            }
            m_dx = m_dy = m_sx = m_sy = 0.0f;
        }

    private:
        static constexpr core::u32 kCount = static_cast<core::u32>(MouseButton::Count);
        static core::u32 Index(MouseButton b) noexcept
        {
            const core::u32 i = static_cast<core::u32>(b);
            return i < kCount ? i : 0;
        }
        core::f32 m_x = 0, m_y = 0, m_dx = 0, m_dy = 0, m_sx = 0, m_sy = 0;
        bool m_current[kCount] = {};
        bool m_previous[kCount] = {};
        bool m_relative = false;
        bool m_cursorVisible = true;
    };

    class WebTouch final : public ITouch
    {
    public:
        [[nodiscard]] core::i32 TouchCount() const override { return 0; }
        [[nodiscard]] bool GetTouchPoint(core::i32, TouchPoint&) const override { return false; }
        [[nodiscard]] bool HasTouch() const override { return false; }
    };

    class WebInputManager final : public IInputManager
    {
    public:
        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse* Mouse() override { return &m_mouse; }
        [[nodiscard]] ITouch* Touch() override { return &m_touch; }
        [[nodiscard]] core::i32 GamepadCount() const override { return 0; }
        [[nodiscard]] IGamepad* GetGamepad(core::i32) override { return nullptr; }
        [[nodiscard]] core::Span<const InputEvent> Events() const override
        {
            return core::Span<const InputEvent>(m_events.Data(), m_events.Size());
        }
        [[nodiscard]] core::u32 HoverWindow() const override { return m_mainWindow; }
        [[nodiscard]] core::u32 FocusedWindow() const override { return m_mainWindow; }

        // Register the HTML5 event callbacks (keyboard on the window, mouse on the canvas). Called
        // once by the shell after the canvas exists. The callbacks feed the async queue below.
        void RegisterCallbacks(core::StringView canvasSelector, core::u32 mainWindowId)
        {
            m_mainWindow = mainWindowId;
            m_selector = core::String(canvasSelector);
            const char* canvas = reinterpret_cast<const char*>(m_selector.CStr());
            emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &OnKey);
            emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &OnKey);
            emscripten_set_mousemove_callback(canvas, this, EM_TRUE, &OnMouseMove);
            emscripten_set_mousedown_callback(canvas, this, EM_TRUE, &OnMouseButton);
            emscripten_set_mouseup_callback(canvas, this, EM_TRUE, &OnMouseButton);
            emscripten_set_wheel_callback(canvas, this, EM_TRUE, &OnWheel);
        }

        // Per frame (shell ProcessEvents): snapshot the previous frame, then apply the events that
        // arrived since the last Update - keeping pressed/released and per-frame deltas correct.
        void Update() override
        {
            m_keyboard.BeginFrame();
            m_mouse.BeginFrame();
            m_events.Clear();
            for (const RawEvent& r : m_queue)
            {
                Apply(r);
            }
            m_queue.Clear();
        }

    private:
        struct RawEvent
        {
            enum class Type : core::u8
            {
                Key,
                MouseMove,
                MouseButton,
                Wheel
            } type{};
            KeyCode key{};
            KeyModifiers mods{};
            core::u32 button = 0;
            bool down = false;
            core::f32 x = 0, y = 0, dx = 0, dy = 0, sx = 0, sy = 0;
        };

        void Apply(const RawEvent& r)
        {
            switch (r.type)
            {
            case RawEvent::Type::Key:
            {
                m_keyboard.SetKey(r.key, r.down);
                m_keyboard.SetModifiers(r.mods);
                InputEvent e;
                e.kind = r.down ? InputEventKind::KeyDown : InputEventKind::KeyUp;
                e.window = m_mainWindow;
                e.key = r.key;
                e.modifiers = r.mods;
                m_events.PushBack(e);
                break;
            }
            case RawEvent::Type::MouseMove:
            {
                m_mouse.OnMotion(r.x, r.y, r.dx, r.dy);
                InputEvent e;
                e.kind = InputEventKind::MouseMove;
                e.window = m_mainWindow;
                e.x = r.x;
                e.y = r.y;
                e.dx = r.dx;
                e.dy = r.dy;
                m_events.PushBack(e);
                break;
            }
            case RawEvent::Type::MouseButton:
            {
                m_mouse.OnButton(r.button, r.down);
                InputEvent e;
                e.kind = r.down ? InputEventKind::MouseButtonDown : InputEventKind::MouseButtonUp;
                e.window = m_mainWindow;
                e.button = static_cast<MouseButton>(r.button);
                m_events.PushBack(e);
                break;
            }
            case RawEvent::Type::Wheel:
            {
                m_mouse.OnWheel(r.sx, r.sy);
                InputEvent e;
                e.kind = InputEventKind::MouseWheel;
                e.window = m_mainWindow;
                e.x = r.sx;
                e.y = r.sy;
                m_events.PushBack(e);
                break;
            }
            }
        }

        [[nodiscard]] static KeyModifiers ModsFrom(const EmscriptenKeyboardEvent* e)
        {
            KeyModifiers m = KeyModifiers::None;
            if (e->shiftKey)
                m = m | KeyModifiers::Shift;
            if (e->ctrlKey)
                m = m | KeyModifiers::Ctrl;
            if (e->altKey)
                m = m | KeyModifiers::Alt;
            if (e->metaKey)
                m = m | KeyModifiers::Gui;
            return m;
        }

        // --- HTML5 callbacks (plain C function pointers; userData is the manager) ---
        static EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* e, void* userData)
        {
            auto* self = static_cast<WebInputManager*>(userData);
            RawEvent r;
            r.type = RawEvent::Type::Key;
            r.key = KeyCodeFromDom(e->code);
            r.down = (eventType == EMSCRIPTEN_EVENT_KEYDOWN);
            r.mods = ModsFrom(e);
            self->m_queue.PushBack(r);
            return EM_TRUE; // consume - a game canvas owns its keys (no page scroll on space/arrows)
        }
        static EM_BOOL OnMouseMove(int, const EmscriptenMouseEvent* e, void* userData)
        {
            auto* self = static_cast<WebInputManager*>(userData);
            RawEvent r;
            r.type = RawEvent::Type::MouseMove;
            r.x = static_cast<core::f32>(e->targetX);
            r.y = static_cast<core::f32>(e->targetY);
            r.dx = static_cast<core::f32>(e->movementX);
            r.dy = static_cast<core::f32>(e->movementY);
            self->m_queue.PushBack(r);
            return EM_TRUE;
        }
        static EM_BOOL OnMouseButton(int eventType, const EmscriptenMouseEvent* e, void* userData)
        {
            auto* self = static_cast<WebInputManager*>(userData);
            RawEvent r;
            r.type = RawEvent::Type::MouseButton;
            // DOM button: 0 left, 1 middle, 2 right (matches MouseButton Left/Middle/Right order).
            r.button = static_cast<core::u32>(e->button);
            r.down = (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN);
            self->m_queue.PushBack(r);
            return EM_TRUE;
        }
        static EM_BOOL OnWheel(int, const EmscriptenWheelEvent* e, void* userData)
        {
            auto* self = static_cast<WebInputManager*>(userData);
            RawEvent r;
            r.type = RawEvent::Type::Wheel;
            // Normalize to "notches" (browsers report pixels/lines); sign matches scroll-up = +.
            r.sx = -static_cast<core::f32>(e->deltaX) / 100.0f;
            r.sy = -static_cast<core::f32>(e->deltaY) / 100.0f;
            self->m_queue.PushBack(r);
            return EM_TRUE;
        }

        WebKeyboard m_keyboard;
        WebMouse m_mouse;
        WebTouch m_touch;
        core::Array<RawEvent> m_queue;   // filled async by the callbacks, drained in Update()
        core::Array<InputEvent> m_events; // this frame's event stream (valid until next Update)
        core::u32 m_mainWindow = 0;
        core::String m_selector; // kept alive for the mouse callbacks' target
    };
}
