// Draconic GUI - :ui_node partition
//
// UINode: a Node with UI chrome. Ported from eepp's UI::UINode - adds padding (a content
// inset), a state-aware skin, and a ControlState that tracks pointer/focus/enabled so
// StateListDrawable skins/backgrounds react to input automatically. (Base Node already
// carries the background/foreground drawables + clip from the render-seam phase.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:ui_node;

import draconic.core;   // RefPtr, Max, Move
import :rect;
import :thickness;
import :control_state;
import :event;
import :drawable;
import :node;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class UINode : public Node
    {
        DRACONIC_OBJECT(UINode, Node)
    public:
        UINode() = default;

        // Content padding (inset for content/children).
        void SetPadding(Thickness padding) { m_padding = padding; Invalidate(); }
        [[nodiscard]] Thickness GetPadding() const noexcept { return m_padding; }

        // The local bounds inset by padding - where content/text goes.
        [[nodiscard]] Rect GetContentBounds() const
        {
            const Rect b = GetLocalBounds();
            return Rect{ b.x + m_padding.Left, b.y + m_padding.Top,
                         core::Max(0.0f, b.width - m_padding.TotalHorizontal()),
                         core::Max(0.0f, b.height - m_padding.TotalVertical()) };
        }

        // Skin = a state-aware background (typically a StateListDrawable). Node::Draw draws
        // the background with GetControlState(), so the skin reacts to hover/press/focus.
        void SetSkin(RefPtr<Drawable> skin) { SetBackground(core::Move(skin)); }

        [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
        [[nodiscard]] bool IsPressed() const noexcept { return m_pressed; }

        // Visual state, driven by pointer/focus/enabled (priority: disabled > pressed >
        // hover > focused > normal).
        [[nodiscard]] ControlState GetControlState() const override
        {
            if (!IsEnabled()) return ControlState::Disabled;
            if (m_pressed)    return ControlState::Pressed;
            if (m_hovered)    return ControlState::Hover;
            if (IsFocused())  return ControlState::Focused;
            return ControlState::Normal;
        }

    protected:
        void OnMouseEnter(const MouseEvent&) override { m_hovered = true; Invalidate(); }
        void OnMouseLeave(const MouseEvent&) override { m_hovered = false; m_pressed = false; Invalidate(); }
        void OnMouseDown(const MouseEvent&) override { m_pressed = true; Invalidate(); }
        void OnMouseUp(const MouseEvent&) override { m_pressed = false; Invalidate(); }

        Thickness m_padding{};
        bool m_hovered = false;
        bool m_pressed = false;
    };

    DRACONIC_DEFINE_OBJECT(UINode, "draconic::gui")
}
