// Draconic GUI - :event partition
//
// Event + EventType + EventCallback: the high-level typed-event foundation nodes register
// listeners on. Derived from eepp's Scene::Event / Event::EventType. The enum starts with
// the geometry/tree events Node fires now; input events are placeholders wired when the
// EventDispatcher lands (Phase 3). MouseEvent/KeyEvent subclasses grow with that phase.

module;
#include "Core/Prelude.h"

export module draconic.gui:event;

import draconic.core;   // Function

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class Node;

    enum class EventType : u32
    {
        // Geometry / tree (fired now)
        PositionChanged,
        SizeChanged,
        VisibilityChanged,
        EnabledChanged,
        ParentChanged,
        Close,
        // Input (placeholders - dispatched from Phase 3)
        MouseDown,
        MouseUp,
        MouseMove,
        MouseEnter,
        MouseLeave,
        MouseClick,
        MouseWheel,
        KeyDown,
        KeyUp,
        TextInput,
        FocusGained,
        FocusLost,
    };

    enum class MouseButton : u32 { Left, Right, Middle, X1, X2 };

    // Key modifier bitmask (unscoped for easy OR-ing).
    enum KeyModifier : u32
    {
        KeyModNone = 0,
        KeyModShift = 1u << 0,
        KeyModCtrl = 1u << 1,
        KeyModAlt = 1u << 2,
        KeyModSuper = 1u << 3,
    };

    // Base event. The typed subclasses below carry payload; a listener registered for a
    // given EventType may static_cast to the matching subclass (the dispatcher always
    // constructs the payload type that matches the EventType).
    struct Event
    {
        EventType Type;
        Node* Target = nullptr;

        explicit Event(EventType type, Node* target = nullptr) noexcept : Type(type), Target(target) {}
        virtual ~Event() = default;
    };

    struct MouseEvent : Event
    {
        core::Float2 Position;
        MouseButton Button;
        u32 Modifiers;

        MouseEvent(EventType type, Node* target, core::Float2 position,
                   MouseButton button = MouseButton::Left, u32 modifiers = 0) noexcept
            : Event(type, target), Position(position), Button(button), Modifiers(modifiers) {}
    };

    struct WheelEvent : Event
    {
        core::Float2 Position;
        core::Float2 Delta;

        WheelEvent(Node* target, core::Float2 position, core::Float2 delta) noexcept
            : Event(EventType::MouseWheel, target), Position(position), Delta(delta) {}
    };

    struct KeyEvent : Event
    {
        u32 KeyCode;
        u32 Modifiers;

        KeyEvent(EventType type, Node* target, u32 keyCode, u32 modifiers = 0) noexcept
            : Event(type, target), KeyCode(keyCode), Modifiers(modifiers) {}
    };

    struct TextInputEvent : Event
    {
        core::StringView Text;

        TextInputEvent(Node* target, core::StringView text) noexcept
            : Event(EventType::TextInput, target), Text(text) {}
    };

    using EventCallback = core::Function<void(const Event&)>;
}
