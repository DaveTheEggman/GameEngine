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

    // Base event. Subclasses (MouseEvent/KeyEvent/...) add payload in Phase 3.
    struct Event
    {
        EventType Type;
        Node* Target = nullptr;

        explicit Event(EventType type, Node* target = nullptr) noexcept : Type(type), Target(target) {}
        virtual ~Event() = default;
    };

    using EventCallback = core::Function<void(const Event&)>;
}
