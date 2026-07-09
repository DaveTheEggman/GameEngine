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

import draconic.core;   // Float2
import :rect;
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

        // Position children in sequence along the orientation, skipping hidden ones.
        void PerformLayout()
        {
            const Rect content = GetContentBounds();
            f32 x = content.x;
            f32 y = content.y;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                Node* child = GetChildAt(i);
                if (child == nullptr || !child->IsVisible()) continue;

                child->SetPosition(core::Float2{ x, y });
                if (m_orientation == Orientation::Vertical) y += child->GetSize().y + m_spacing;
                else x += child->GetSize().x + m_spacing;
            }
        }

    protected:
        void OnSizeChange() override { PerformLayout(); }
        void OnChildrenChanged() override { PerformLayout(); }

        Orientation m_orientation = Orientation::Vertical;
        f32 m_spacing = 0.0f;
    };

    DRACONIC_DEFINE_OBJECT(LinearLayout, "draconic::gui")
}
