// Draconic GUI - :scroll_view partition
//
// ScrollView: a container that clips its content to the viewport and offsets it by a scroll
// amount, so a large content area can be panned within a smaller box. Modeled on eepp's
// UIScrollableWidget / UIScrollView (role, not a line-for-line port). Content lives in a
// single child container (GetContent()); set that container's size to the scrollable extent
// and add your widgets to it. The wheel scrolls (bubbled from the hovered content via
// Node::WantsWheel), and SetScrollOffset drives it programmatically - e.g. from a ScrollBar's
// value. Auto-managed scrollbars are a follow-up; here the view exposes the scroll range so a
// ScrollBar can be wired externally.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:scroll_view;

import draconic.core;   // RefPtr, MakeRef, Function, Move, Max, Min, Float2
import :rect;
import :node;
import :event;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class ScrollView : public UIWidget
    {
        DRACONIC_OBJECT(ScrollView, UIWidget)
    public:
        ScrollView()
        {
            SetTag(core::StringView(u8"scrollview"));
            SetClipChildren(true);
            m_content = core::MakeRef<UIWidget>(core::DefaultAllocator());
            AddChild(m_content.Get());
        }

        // The container holding the scrollable content: set its size to the scrollable extent
        // and add your widgets to it.
        [[nodiscard]] Node* GetContent() const noexcept { return m_content.Get(); }

        // Convenience: set the scrollable extent directly.
        void SetContentSize(core::Float2 size) { m_content->SetSize(size); ClampAndApply(); }
        [[nodiscard]] core::Float2 GetContentSize() const { return m_content->GetSize(); }

        // The maximum scrollable distance on each axis: max(0, content - viewport).
        [[nodiscard]] core::Float2 ScrollRange() const
        {
            const Rect vp = GetContentBounds();
            const core::Float2 cs = m_content->GetSize();
            return core::Float2{ core::Max(0.0f, cs.x - vp.width), core::Max(0.0f, cs.y - vp.height) };
        }

        [[nodiscard]] core::Float2 GetScrollOffset() const noexcept { return m_offset; }
        void SetScrollOffset(core::Float2 offset)
        {
            const core::Float2 clamped = Clamp(offset);
            if (clamped.x == m_offset.x && clamped.y == m_offset.y) return;
            m_offset = clamped;
            ApplyScroll();
            if (m_onScroll) m_onScroll(m_offset);
        }
        void ScrollBy(core::Float2 delta) { SetScrollOffset(core::Float2{ m_offset.x + delta.x, m_offset.y + delta.y }); }

        // Scroll fraction on each axis in [0,1] (0 when the axis cannot scroll). Handy to sync
        // a ScrollBar both ways.
        [[nodiscard]] core::Float2 GetScrollFraction() const
        {
            const core::Float2 r = ScrollRange();
            return core::Float2{ r.x > 0.0f ? m_offset.x / r.x : 0.0f, r.y > 0.0f ? m_offset.y / r.y : 0.0f };
        }
        void SetVerticalFraction(f32 fraction)
        {
            SetScrollOffset(core::Float2{ m_offset.x, ScrollRange().y * core::Max(0.0f, core::Min(1.0f, fraction)) });
        }
        void SetHorizontalFraction(f32 fraction)
        {
            SetScrollOffset(core::Float2{ ScrollRange().x * core::Max(0.0f, core::Min(1.0f, fraction)), m_offset.y });
        }

        void SetWheelSpeed(f32 pixelsPerNotch) noexcept { m_wheelSpeed = pixelsPerNotch; }
        void SetOnScroll(core::Function<void(core::Float2)> callback) { m_onScroll = core::Move(callback); }

        [[nodiscard]] bool WantsWheel() const override { return true; }

    protected:
        void OnMouseWheel(const WheelEvent& event) override
        {
            // Wheel up (positive y) reveals content above -> decreases the offset.
            ScrollBy(core::Float2{ -event.Delta.x * m_wheelSpeed, -event.Delta.y * m_wheelSpeed });
        }

        void OnSizeChange() override { ClampAndApply(); }

    private:
        [[nodiscard]] core::Float2 Clamp(core::Float2 offset) const
        {
            const core::Float2 r = ScrollRange();
            return core::Float2{ core::Max(0.0f, core::Min(r.x, offset.x)), core::Max(0.0f, core::Min(r.y, offset.y)) };
        }
        void ApplyScroll()
        {
            const Rect vp = GetContentBounds();
            m_content->SetPosition(core::Float2{ vp.x - m_offset.x, vp.y - m_offset.y });
        }
        void ClampAndApply() { m_offset = Clamp(m_offset); ApplyScroll(); }

        RefPtr<UIWidget> m_content;
        core::Float2 m_offset{ 0.0f, 0.0f };
        f32 m_wheelSpeed = 40.0f;
        core::Function<void(core::Float2)> m_onScroll;
    };

    DRACONIC_DEFINE_OBJECT(ScrollView, "draconic::gui")
}
