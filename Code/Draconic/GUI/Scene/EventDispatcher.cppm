// Draconic GUI - :event_dispatcher partition
//
// EventDispatcher: routes abstract input into the node tree. Ported from eepp's
// Scene::EventDispatcher, but the core is platform-agnostic: instead of hooking a window's
// Input, it exposes an Inject* API that a gui.shell bridge feeds with already-abstracted
// events (from InputSurface/InputRouter). It hit-tests via the root's OverFind, tracks
// hover / press / focus, and calls the target Node's Handle* dispatch methods.
//
// Interaction refs (over/down/focus) are non-owning Node*; a full node-removal cleanup
// hook is deferred (a removed hovered/focused node should clear these) - noted for the
// lifecycle wiring.

module;
#include "Core/Prelude.h"

export module draconic.gui:event_dispatcher;

import draconic.core;   // Float2, StringView
import :node;
import :event;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class EventDispatcher
    {
    public:
        explicit EventDispatcher(Node* root) noexcept : m_root(root) {}

        [[nodiscard]] Node* GetRoot() const noexcept { return m_root; }
        [[nodiscard]] Node* GetOverNode() const noexcept { return m_overNode; }
        [[nodiscard]] Node* GetFocusNode() const noexcept { return m_focusNode; }
        [[nodiscard]] core::Float2 GetMousePosition() const noexcept { return m_mousePos; }

        // True if the currently focused node wants platform text input (the gui.shell bridge
        // reconciles the window's IME state against this each frame).
        [[nodiscard]] bool WantsTextInput() const { return m_focusNode != nullptr && m_focusNode->WantsTextInput(); }

        // === Injection API (fed by the shell bridge) ===
        void InjectMouseMove(core::Float2 position)
        {
            m_mousePos = position;
            Node* hit = HitTest(position);
            if (hit != m_overNode)
            {
                if (m_overNode) m_overNode->HandleMouseLeave(MouseEvent(EventType::MouseLeave, m_overNode, position));
                m_overNode = hit;
                if (m_overNode) m_overNode->HandleMouseEnter(MouseEvent(EventType::MouseEnter, m_overNode, position));
            }
            // Pointer capture: while a button is held, the pressed node keeps receiving moves
            // (so a drag continues even when the cursor leaves it). Otherwise the hovered node.
            Node* target = (m_downNode != nullptr) ? m_downNode : hit;
            if (target) target->HandleMouseMove(MouseEvent(EventType::MouseMove, target, position));
        }

        void InjectMouseDown(core::Float2 position, MouseButton button, u32 modifiers = 0)
        {
            m_mousePos = position;
            Node* hit = HitTest(position);
            m_downNode = hit;
            SetFocusNode(hit); // click-to-focus
            if (hit) hit->HandleMouseDown(MouseEvent(EventType::MouseDown, hit, position, button, modifiers));
        }

        void InjectMouseUp(core::Float2 position, MouseButton button, u32 modifiers = 0)
        {
            m_mousePos = position;
            Node* hit = HitTest(position);
            // The captured (pressed) node gets the release, even if the cursor moved off it.
            Node* target = (m_downNode != nullptr) ? m_downNode : hit;
            if (target) target->HandleMouseUp(MouseEvent(EventType::MouseUp, target, position, button, modifiers));
            // A click only when the release lands on the node that was pressed.
            if (hit != nullptr && hit == m_downNode)
                hit->HandleMouseClick(MouseEvent(EventType::MouseClick, hit, position, button, modifiers));
            m_downNode = nullptr;
        }

        void InjectMouseWheel(core::Float2 position, core::Float2 delta)
        {
            Node* hit = HitTest(position);
            if (hit) hit->HandleMouseWheel(WheelEvent(hit, position, delta));
        }

        void InjectKeyDown(u32 keyCode, u32 modifiers = 0)
        {
            if (m_focusNode) m_focusNode->HandleKeyDown(KeyEvent(EventType::KeyDown, m_focusNode, keyCode, modifiers));
        }
        void InjectKeyUp(u32 keyCode, u32 modifiers = 0)
        {
            if (m_focusNode) m_focusNode->HandleKeyUp(KeyEvent(EventType::KeyUp, m_focusNode, keyCode, modifiers));
        }
        void InjectText(core::StringView text)
        {
            if (m_focusNode) m_focusNode->HandleTextInput(TextInputEvent(m_focusNode, text));
        }

        // === Focus ===
        void SetFocusNode(Node* node)
        {
            if (node == m_focusNode) return;
            Node* previous = m_focusNode;
            m_focusNode = node;
            if (previous) previous->HandleFocusLost();
            if (m_focusNode) m_focusNode->HandleFocusGained();
        }

        // Clear any interaction refs pointing at `node` (call before removing/destroying it).
        void NotifyNodeRemoved(Node* node)
        {
            if (m_overNode == node) m_overNode = nullptr;
            if (m_downNode == node) m_downNode = nullptr;
            if (m_focusNode == node) m_focusNode = nullptr;
        }

    private:
        [[nodiscard]] Node* HitTest(core::Float2 position) const
        {
            return m_root ? m_root->OverFind(position) : nullptr;
        }

        Node* m_root;                  // non-owning (the SceneNode owns this dispatcher)
        Node* m_overNode = nullptr;    // non-owning
        Node* m_downNode = nullptr;    // non-owning
        Node* m_focusNode = nullptr;   // non-owning
        core::Float2 m_mousePos{ 0.0f, 0.0f };
    };
}
