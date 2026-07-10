// Draconic GUI - :linear_layout partition
//
// LinearLayout: stacks its children in a row or column with spacing, starting from the
// padding-inset content bounds. Modeled on eepp's UILinearLayout (role, not a line-for-line
// port). Re-runs on size change and on child add/remove (via Node::OnChildrenChanged).
// Children keep their own sizes here; measurement (wrap-content), weights/stretch, and
// gravity are deferred.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:linear_layout;

import draconic.core;   // Float2, Max
import :rect;
import :thickness;
import :node;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    enum class Orientation { Horizontal, Vertical };

    class LinearLayout : public UIWidget
    {
        DRACONIC_OBJECT(LinearLayout, UIWidget)
    public:
        LinearLayout() = default;

        void SetOrientation(Orientation orientation) { m_orientation = orientation; PerformLayout(); }
        [[nodiscard]] Orientation GetOrientation() const noexcept { return m_orientation; }

        void SetSpacing(f32 spacing) { m_spacing = spacing; PerformLayout(); }
        [[nodiscard]] f32 GetSpacing() const noexcept { return m_spacing; }

        // Wrap-content: when on, the layout resizes itself along its orientation to exactly fit
        // its stacked children (plus padding), leaving the cross axis untouched. Useful inside a
        // ScrollView with SetAutoMeasureContent so a tall stack scrolls. Off by default.
        void SetWrapContent(bool wrap) { m_wrapContent = wrap; PerformLayout(); }
        [[nodiscard]] bool IsWrapContent() const noexcept { return m_wrapContent; }

        // Position children in sequence along the orientation, skipping hidden ones.
        void PerformLayout()
        {
            if (m_layingOut) return; // re-entrancy guard (a wrap-content SetSize re-enters)
            m_layingOut = true;

            const Rect content = GetContentBounds();
            f32 x = content.x;
            f32 y = content.y;
            f32 extent = 0.0f; // main-axis size of the stacked children (no trailing spacing)
            for (usize i = 0; i < ChildCount(); ++i)
            {
                Node* child = GetChildAt(i);
                if (child == nullptr || !child->IsVisible()) continue;

                child->SetPosition(core::Float2{ x, y });
                if (m_orientation == Orientation::Vertical) { const f32 h = child->GetSize().y; y += h + m_spacing; extent = (y - content.y) - m_spacing; }
                else                                        { const f32 w = child->GetSize().x; x += w + m_spacing; extent = (x - content.x) - m_spacing; }
            }
            m_layingOut = false;

            if (m_wrapContent)
            {
                const Thickness p = GetPadding();
                if (m_orientation == Orientation::Vertical)
                    SetSize(core::Float2{ GetSize().x, core::Max(0.0f, extent) + p.TotalVertical() });
                else
                    SetSize(core::Float2{ core::Max(0.0f, extent) + p.TotalHorizontal(), GetSize().y });
            }
        }

    protected:
        void OnSizeChange() override { PerformLayout(); }
        void OnChildrenChanged() override { PerformLayout(); }

        Orientation m_orientation = Orientation::Vertical;
        f32 m_spacing = 0.0f;
        bool m_wrapContent = false;
        bool m_layingOut = false;
    };

    DRACONIC_DEFINE_OBJECT(LinearLayout, "draconic::gui")
}
