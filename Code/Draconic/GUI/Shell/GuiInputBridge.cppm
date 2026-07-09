// Draconic GUI - `draconic.gui.shell`: the platform input bridge.
//
// Keeps the GUI core platform-agnostic: the core EventDispatcher exposes an abstract
// Inject* API; this bridge is the ONLY place that knows about the platform input layer
// (draconic.shell). It translates a stream of shell::InputEvent (already gated/transformed
// by InputSurface/InputRouter, see [[viewport-input]]) into dispatcher injections, mapping
// platform enums to GUI enums and window-space positions to content space via a ContentFit.
//
// Reimplemented for Draconic (not ported from eepp/Sedulous), per the port plan: the
// InputSurface consumption lives here, never in the core.

module;
#include "Core/Prelude.h"

export module draconic.gui.shell;

import draconic.core;    // ContentFit, Float2, StringView, u32
import draconic.gui;     // EventDispatcher, MouseButton, KeyMod*
import draconic.shell;   // InputEvent, InputEventKind, MouseButton, KeyModifiers

using namespace draconic::core;
namespace core = draconic::core;
namespace platform = draconic::shell;

export namespace draconic::gui
{
    // Translates platform input events into EventDispatcher injections.
    class GuiInputBridge
    {
    public:
        explicit GuiInputBridge(EventDispatcher* dispatcher) noexcept : m_dispatcher(dispatcher) {}

        // The content fit maps window/region-space event positions into GUI content space.
        // Without one, positions pass through unchanged (identity).
        void SetContentFit(const core::ContentFit& fit) noexcept { m_fit = fit; m_hasFit = true; }
        void ClearContentFit() noexcept { m_hasFit = false; }

        // Translate one platform input event into a dispatcher injection. Returns true if the
        // event was routed to the GUI, false if it was ignored (gamepad/touch/unknown).
        bool Dispatch(const platform::InputEvent& event)
        {
            if (m_dispatcher == nullptr) return false;
            switch (event.kind)
            {
            case platform::InputEventKind::MouseMove:
                m_dispatcher->InjectMouseMove(ToContent(core::Float2{ event.x, event.y }));
                return true;
            case platform::InputEventKind::MouseButtonDown:
                m_dispatcher->InjectMouseDown(ToContent(core::Float2{ event.x, event.y }),
                    MapButton(event.button), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::MouseButtonUp:
                m_dispatcher->InjectMouseUp(ToContent(core::Float2{ event.x, event.y }),
                    MapButton(event.button), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::MouseWheel:
                // For wheel, x/y is the scroll delta; position is the last known cursor spot.
                m_dispatcher->InjectMouseWheel(m_dispatcher->GetMousePosition(), core::Float2{ event.x, event.y });
                return true;
            case platform::InputEventKind::KeyDown:
                m_dispatcher->InjectKeyDown(MapKey(event.key), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::KeyUp:
                m_dispatcher->InjectKeyUp(MapKey(event.key), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::TextInput:
                m_dispatcher->InjectText(core::StringView(event.text));
                return true;
            default:
                return false; // gamepad / touch not routed to the GUI yet
            }
        }

        // Poll a gated InputSurface (its mouse is already content-space) and drive the
        // dispatcher: hover from the cursor, click from press/release edges, plus wheel. The
        // recommended path for a viewport-hosted GUI - the surface handles transform + gating,
        // and a fresh InjectMouseMove each frame keeps hover current. (Keyboard/text still come
        // through the event-based Dispatch() path.)
        void PumpFromSurface(platform::InputSurface& surface)
        {
            if (m_dispatcher == nullptr) return;
            platform::IMouse* mouse = surface.Mouse();
            if (mouse == nullptr) return;

            const core::Float2 position{ mouse->X(), mouse->Y() };
            m_dispatcher->InjectMouseMove(position);

            const platform::MouseButton buttons[3] = {
                platform::MouseButton::Left, platform::MouseButton::Middle, platform::MouseButton::Right };
            for (const platform::MouseButton button : buttons)
            {
                if (mouse->IsButtonPressed(button))  m_dispatcher->InjectMouseDown(position, MapButton(button));
                if (mouse->IsButtonReleased(button)) m_dispatcher->InjectMouseUp(position, MapButton(button));
            }

            const f32 scrollX = mouse->ScrollX();
            const f32 scrollY = mouse->ScrollY();
            if (scrollX != 0.0f || scrollY != 0.0f)
                m_dispatcher->InjectMouseWheel(position, core::Float2{ scrollX, scrollY });
        }

    private:
        [[nodiscard]] core::Float2 ToContent(core::Float2 windowPos) const
        {
            if (!m_hasFit) return windowPos;
            core::Float2 out{ 0.0f, 0.0f };
            [[maybe_unused]] const bool inside = m_fit.ToContent(windowPos, out); // out set regardless
            return out;
        }

        // shell::MouseButton order is Left/Middle/Right - map explicitly, not by value.
        [[nodiscard]] static MouseButton MapButton(platform::MouseButton button) noexcept
        {
            switch (button)
            {
            case platform::MouseButton::Left:   return MouseButton::Left;
            case platform::MouseButton::Right:  return MouseButton::Right;
            case platform::MouseButton::Middle: return MouseButton::Middle;
            case platform::MouseButton::X1:     return MouseButton::X1;
            case platform::MouseButton::X2:     return MouseButton::X2;
            default:                            return MouseButton::Left;
            }
        }

        // Map the platform key code to the GUI's platform-agnostic KeyCode (as a u32).
        // Only the navigation/editing keys the GUI interprets are mapped; everything else
        // (printable characters) arrives via TextInput, so it maps to Unknown here.
        [[nodiscard]] static u32 MapKey(platform::KeyCode key) noexcept
        {
            KeyCode mapped = KeyCode::Unknown;
            switch (key)
            {
            case platform::KeyCode::Return:    mapped = KeyCode::Return;    break;
            case platform::KeyCode::Escape:    mapped = KeyCode::Escape;    break;
            case platform::KeyCode::Backspace: mapped = KeyCode::Backspace; break;
            case platform::KeyCode::Tab:       mapped = KeyCode::Tab;       break;
            case platform::KeyCode::Space:     mapped = KeyCode::Space;     break;
            case platform::KeyCode::Delete:    mapped = KeyCode::Delete;    break;
            case platform::KeyCode::Insert:    mapped = KeyCode::Insert;    break;
            case platform::KeyCode::Home:      mapped = KeyCode::Home;      break;
            case platform::KeyCode::End:       mapped = KeyCode::End;       break;
            case platform::KeyCode::PageUp:    mapped = KeyCode::PageUp;    break;
            case platform::KeyCode::PageDown:  mapped = KeyCode::PageDown;  break;
            case platform::KeyCode::Left:      mapped = KeyCode::Left;      break;
            case platform::KeyCode::Right:     mapped = KeyCode::Right;     break;
            case platform::KeyCode::Up:        mapped = KeyCode::Up;        break;
            case platform::KeyCode::Down:      mapped = KeyCode::Down;      break;
            default:                           mapped = KeyCode::Unknown;   break;
            }
            return static_cast<u32>(mapped);
        }

        [[nodiscard]] static u32 MapModifiers(platform::KeyModifiers mods) noexcept
        {
            u32 out = 0;
            if ((mods & platform::KeyModifiers::Shift) != platform::KeyModifiers::None) out |= KeyModShift;
            if ((mods & platform::KeyModifiers::Ctrl)  != platform::KeyModifiers::None) out |= KeyModCtrl;
            if ((mods & platform::KeyModifiers::Alt)   != platform::KeyModifiers::None) out |= KeyModAlt;
            if ((mods & platform::KeyModifiers::Gui)   != platform::KeyModifiers::None) out |= KeyModSuper;
            return out;
        }

        EventDispatcher* m_dispatcher;
        core::ContentFit m_fit{};
        bool m_hasFit = false;
    };
}
