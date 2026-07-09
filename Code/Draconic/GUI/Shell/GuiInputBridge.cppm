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
                m_dispatcher->InjectKeyDown(static_cast<u32>(event.key), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::KeyUp:
                m_dispatcher->InjectKeyUp(static_cast<u32>(event.key), MapModifiers(event.modifiers));
                return true;
            case platform::InputEventKind::TextInput:
                m_dispatcher->InjectText(core::StringView(event.text));
                return true;
            default:
                return false; // gamepad / touch not routed to the GUI yet
            }
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
