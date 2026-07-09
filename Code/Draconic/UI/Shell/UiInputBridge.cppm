// Draconic UI - `draconic.ui.shell`: the platform input bridge for draconic.ui.
//
// Keeps the core draconic.ui platform-agnostic: it exposes UIContext's InputManager (physical-pixel
// Process* API) and this bridge is the only place that knows draconic.shell. It translates a stream of
// shell::InputEvent (already gated/transformed by InputSurface/InputRouter, see [[viewport-input]]) into
// InputManager calls, and drives the window's text input (IME) from focus via UIContext::WantsTextInput()
// - the mechanism Sedulous shell never finished. Reimplemented for Draconic (NOT ported from Sedulous.
// UI.Shell), mirroring the draconic.gui GuiInputBridge, per the port plan: InputSurface consumption lives
// here, never in the core.

module;
#include "Core/Prelude.h"

export module draconic.ui.shell;

import draconic.core;    // Float2, StringView, DecodeUtf8
import draconic.ui;      // UIContext, InputManager, KeyCode, MouseButton, KeyModifiers
import draconic.shell;   // InputEvent, InputSurface, IMouse, IWindow

using namespace draconic::core;
namespace platform = draconic::shell;

export namespace draconic::ui
{
    /// Translates platform input events into UIContext::InputManager calls.
    class UiInputBridge
    {
    public:
        explicit UiInputBridge(UIContext* context) noexcept : m_context(context) {}

        /// The window whose platform text input (IME) follows UI focus. Once set, the bridge starts text
        /// input while a text-editing view holds focus and stops it otherwise - reconciled after each
        /// Dispatch()/PumpFromSurface(). Pass nullptr to disable.
        void SetTextInputTarget(platform::IWindow* window) noexcept { m_textInputTarget = window; }

        /// Reconcile the target window's text-input state with the focused view's WantsTextInput().
        void SyncTextInput()
        {
            if (m_textInputTarget == nullptr || m_context == nullptr) { return; }
            const bool want = m_context->WantsTextInput();
            if (want && !m_textInputTarget->IsTextInputActive()) { m_textInputTarget->StartTextInput(); }
            else if (!want && m_textInputTarget->IsTextInputActive()) { m_textInputTarget->StopTextInput(); }
        }

        /// Translate one platform input event into an InputManager call. Returns true if routed, false if
        /// ignored (gamepad/touch/unknown). Reconciles text-input afterwards (a click/Tab can move focus).
        bool Dispatch(const platform::InputEvent& event)
        {
            if (m_context == nullptr) { return false; }
            InputManager* im = m_context->GetInputManager();
            bool routed = true;
            switch (event.kind)
            {
            case platform::InputEventKind::MouseMove:
                m_lastX = event.x; m_lastY = event.y;
                (void)im->ProcessMouseMove(event.x, event.y);
                break;
            case platform::InputEventKind::MouseButtonDown:
                m_lastX = event.x; m_lastY = event.y;
                (void)im->ProcessMouseDown(MapButton(event.button), event.x, event.y, m_context->TotalTime());
                break;
            case platform::InputEventKind::MouseButtonUp:
                m_lastX = event.x; m_lastY = event.y;
                (void)im->ProcessMouseUp(MapButton(event.button), event.x, event.y);
                break;
            case platform::InputEventKind::MouseWheel:
                // For wheel, x/y is the scroll delta; position is the last known cursor spot.
                (void)im->ProcessMouseWheel(m_lastX, m_lastY, event.x, event.y, MapModifiers(event.modifiers));
                break;
            case platform::InputEventKind::KeyDown:
                (void)im->ProcessKeyDown(MapKey(event.key), MapModifiers(event.modifiers), false, m_context->TotalTime());
                break;
            case platform::InputEventKind::KeyUp:
                (void)im->ProcessKeyUp(MapKey(event.key), MapModifiers(event.modifiers), m_context->TotalTime());
                break;
            case platform::InputEventKind::TextInput:
            {
                const StringView text{ event.text };
                usize i = 0;
                while (i < text.Size()) { (void)im->ProcessTextInput(static_cast<char32_t>(DecodeUtf8(text, i))); }
                break;
            }
            default:
                routed = false; // gamepad / touch not routed to the UI
                break;
            }
            SyncTextInput();
            return routed;
        }

        /// Poll a gated InputSurface (its mouse is already content-space) and drive the InputManager:
        /// hover from the cursor, click from press/release edges, plus wheel. Keyboard/text still come
        /// through Dispatch().
        void PumpFromSurface(platform::InputSurface& surface)
        {
            if (m_context == nullptr) { return; }
            platform::IMouse* mouse = surface.Mouse();
            if (mouse == nullptr) { return; }
            InputManager* im = m_context->GetInputManager();

            const f32 x = mouse->X();
            const f32 y = mouse->Y();
            m_lastX = x; m_lastY = y;
            (void)im->ProcessMouseMove(x, y);

            const platform::MouseButton buttons[3] = {
                platform::MouseButton::Left, platform::MouseButton::Middle, platform::MouseButton::Right };
            for (const platform::MouseButton button : buttons)
            {
                if (mouse->IsButtonPressed(button))  { (void)im->ProcessMouseDown(MapButton(button), x, y, m_context->TotalTime()); }
                if (mouse->IsButtonReleased(button)) { (void)im->ProcessMouseUp(MapButton(button), x, y); }
            }

            const f32 scrollX = mouse->ScrollX();
            const f32 scrollY = mouse->ScrollY();
            if (scrollX != 0.0f || scrollY != 0.0f)
            {
                // Carry the live keyboard modifiers so Shift+wheel scrolls horizontally, etc.
                platform::IKeyboard* keyboard = surface.Keyboard();
                const KeyModifiers mods = (keyboard != nullptr) ? MapModifiers(keyboard->Modifiers()) : KeyModifiers::None;
                (void)im->ProcessMouseWheel(x, y, scrollX, scrollY, mods);
            }

            SyncTextInput();
        }

    private:
        [[nodiscard]] static MouseButton MapButton(platform::MouseButton button) noexcept
        {
            switch (button)
            {
            case platform::MouseButton::Left:   return MouseButton::Left;
            case platform::MouseButton::Middle: return MouseButton::Middle;
            case platform::MouseButton::Right:  return MouseButton::Right;
            case platform::MouseButton::X1:     return MouseButton::X1;
            case platform::MouseButton::X2:     return MouseButton::X2;
            default:                            return MouseButton::Left;
            }
        }

        // The shell and UI KeyCode enums share names but differ in value. Letters A-Z are contiguous in
        // both, so map that range arithmetically; map the nav/editing keys the UI interprets explicitly.
        [[nodiscard]] static KeyCode MapKey(platform::KeyCode key) noexcept
        {
            using SK = platform::KeyCode;
            const u32 kv = static_cast<u32>(key);
            if (kv >= static_cast<u32>(SK::A) && kv <= static_cast<u32>(SK::Z))
            {
                return static_cast<KeyCode>(static_cast<u32>(KeyCode::A) + (kv - static_cast<u32>(SK::A)));
            }
            switch (key)
            {
            case SK::Return:    return KeyCode::Return;
            case SK::Escape:    return KeyCode::Escape;
            case SK::Backspace: return KeyCode::Backspace;
            case SK::Tab:       return KeyCode::Tab;
            case SK::Space:     return KeyCode::Space;
            case SK::Delete:    return KeyCode::Delete;
            case SK::Insert:    return KeyCode::Insert;
            case SK::Home:      return KeyCode::Home;
            case SK::End:       return KeyCode::End;
            case SK::PageUp:    return KeyCode::PageUp;
            case SK::PageDown:  return KeyCode::PageDown;
            case SK::Left:      return KeyCode::Left;
            case SK::Right:     return KeyCode::Right;
            case SK::Up:        return KeyCode::Up;
            case SK::Down:      return KeyCode::Down;
            default:            return KeyCode::Unknown;
            }
        }

        [[nodiscard]] static KeyModifiers MapModifiers(platform::KeyModifiers mods) noexcept
        {
            KeyModifiers out = KeyModifiers::None;
            if ((mods & platform::KeyModifiers::Shift) != platform::KeyModifiers::None) { out = out | KeyModifiers::Shift; }
            if ((mods & platform::KeyModifiers::Ctrl)  != platform::KeyModifiers::None) { out = out | KeyModifiers::Ctrl; }
            if ((mods & platform::KeyModifiers::Alt)   != platform::KeyModifiers::None) { out = out | KeyModifiers::Alt; }
            if ((mods & platform::KeyModifiers::Gui)   != platform::KeyModifiers::None) { out = out | KeyModifiers::Gui; }
            return out;
        }

        UIContext* m_context;
        platform::IWindow* m_textInputTarget = nullptr;
        f32 m_lastX = 0.0f;
        f32 m_lastY = 0.0f;
    };
}
