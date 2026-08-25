// Editor::App - FloatingPanel implementation (see FloatingPanel.cppm). The drag/resize handles are
// small Label subclasses that capture the mouse (the Slider pattern) and drive the owning panel.

module;
#include "Core/Prelude.h"

module editor.app;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

namespace editor
{
    namespace ui = foundation::ui;

    namespace
    {
        // Header title strip: dragging it moves the owning panel (clamped to its parent).
        class DragMoveStrip final : public ui::Label
        {
        public:
            explicit DragMoveStrip(StringView text) : ui::Label(text) {}
            FloatingPanel* owner = nullptr;

            void OnMouseDown(ui::MouseEventArgs& e) override
            {
                if (e.Button == ui::MouseButton::Left)
                {
                    m_dragging = true;
                    m_lastX = e.X;
                    m_lastY = e.Y;
                    if (Context != nullptr)
                    {
                        Context->GetFocusManager()->SetCapture(this);
                    }
                    e.Handled = true;
                }
            }
            void OnMouseMove(ui::MouseEventArgs& e) override
            {
                if (m_dragging && owner != nullptr)
                {
                    owner->DragBy(e.X - m_lastX, e.Y - m_lastY);
                    m_lastX = e.X;
                    m_lastY = e.Y;
                    e.Handled = true;
                }
            }
            void OnMouseUp(ui::MouseEventArgs& e) override
            {
                if (e.Button == ui::MouseButton::Left && m_dragging)
                {
                    m_dragging = false;
                    if (Context != nullptr)
                    {
                        Context->GetFocusManager()->ReleaseCapture();
                    }
                    e.Handled = true;
                }
            }

        private:
            bool m_dragging = false;
            f32 m_lastX = 0.0f;
            f32 m_lastY = 0.0f;
        };

        // Bottom-right grip: dragging it resizes the owning panel.
        class ResizeGrip final : public ui::Label
        {
        public:
            explicit ResizeGrip(StringView text) : ui::Label(text) {}
            FloatingPanel* owner = nullptr;

            void OnMouseDown(ui::MouseEventArgs& e) override
            {
                if (e.Button == ui::MouseButton::Left)
                {
                    m_dragging = true;
                    m_lastX = e.X;
                    m_lastY = e.Y;
                    if (Context != nullptr)
                    {
                        Context->GetFocusManager()->SetCapture(this);
                    }
                    e.Handled = true;
                }
            }
            void OnMouseMove(ui::MouseEventArgs& e) override
            {
                if (m_dragging && owner != nullptr)
                {
                    owner->ResizeBy(e.X - m_lastX, e.Y - m_lastY);
                    m_lastX = e.X;
                    m_lastY = e.Y;
                    e.Handled = true;
                }
            }
            void OnMouseUp(ui::MouseEventArgs& e) override
            {
                if (e.Button == ui::MouseButton::Left && m_dragging)
                {
                    m_dragging = false;
                    if (Context != nullptr)
                    {
                        Context->GetFocusManager()->ReleaseCapture();
                    }
                    e.Handled = true;
                }
            }

        private:
            bool m_dragging = false;
            f32 m_lastX = 0.0f;
            f32 m_lastY = 0.0f;
        };
    }

    FloatingPanel::FloatingPanel(StringView title)
    {
        AddClass(u8"panel"); // background drawable from the "panel" style class
        Padding = ui::Thickness{6.0f, 6.0f, 6.0f, 6.0f};

        // Outer frame so the resize grip can overlay the bottom-right corner without inflating size
        // (FrameLayout measures to its largest child - the content column, not the small grip).
        auto frame = MakeRef<ui::FrameLayout>(DefaultAllocator());

        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;

        // --- header: [drag title | collapse | close] ---
        auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
        header->Direction = ui::Orientation::Horizontal;

        auto strip = MakeRef<DragMoveStrip>(DefaultAllocator(), title);
        strip->owner = this;
        strip->FontSize.SetValue(Optional<f32>{12.0f});
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f; // title absorbs the slack; the two buttons sit at the right edge
            header->AddView(strip.Get(), lp);
        }

        m_collapseBtn = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"-"));
        m_collapseBtn->FontSize.SetValue(Optional<f32>{11.0f});
        m_collapseBtn->OnClick.Add([this](ui::ButtonBase*) { SetCollapsed(!m_collapsed); });
        header->AddView(m_collapseBtn.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

        auto closeBtn = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"x"));
        closeBtn->FontSize.SetValue(Optional<f32>{11.0f});
        closeBtn->OnClick.Add([this](ui::ButtonBase*) { OnClose.Invoke(); });
        header->AddView(closeBtn.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

        column->AddView(header.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

        // --- body (content host) ---
        m_body = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_body->Direction = ui::Orientation::Vertical;
        column->AddView(m_body.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

        {
            auto cfp = MakeRef<ui::FrameLayoutParams>(DefaultAllocator());
            cfp->Gravity = ui::Gravity::Fill;
            frame->AddView(column.Get(), cfp);
        }

        // --- resize grip (bottom-right) ---
        auto grip = MakeRef<ResizeGrip>(DefaultAllocator(), StringView(u8"//"));
        grip->owner = this;
        grip->FontSize.SetValue(Optional<f32>{11.0f});
        {
            auto gfp = MakeRef<ui::FrameLayoutParams>(DefaultAllocator());
            gfp->Gravity = ui::Gravity::BottomRight;
            frame->AddView(grip.Get(), gfp);
        }

        Panel::AddView(frame.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));
    }

    void FloatingPanel::SetContent(RefPtr<ui::View> content)
    {
        if (!m_body)
        {
            return;
        }
        m_body->RemoveAllViews(true);
        if (content)
        {
            m_body->AddView(content.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));
        }
    }

    void FloatingPanel::SetCollapsed(bool collapsed)
    {
        if (m_collapsed == collapsed)
        {
            return;
        }
        m_collapsed = collapsed;
        if (m_body)
        {
            m_body->Visibility = collapsed ? ui::Visibility::Gone : ui::Visibility::Visible;
        }
        if (m_collapseBtn)
        {
            m_collapseBtn->SetText(collapsed ? StringView(u8"+") : StringView(u8"-"));
        }
        ApplySize(); // collapsed -> height wraps to just the header
    }

    void FloatingPanel::DragBy(f32 dx, f32 dy)
    {
        Transform.Translation.x += dx;
        Transform.Translation.y += dy;
        ClampToParent();
    }

    void FloatingPanel::ResizeBy(f32 dx, f32 dy)
    {
        if (m_width <= 0.0f)
        {
            m_width = Width(); // first resize: capture the content-sized dimensions, then grow
        }
        if (m_height <= 0.0f)
        {
            m_height = Height();
        }
        m_width = Max(140.0f, m_width + dx);
        m_height = Max(70.0f, m_height + dy);
        // Don't let a resize push the panel past its parent (same reason we clamp drags).
        if (Parent != nullptr)
        {
            const f32 maxW = Parent->Bounds.width - (Bounds.x + Transform.Translation.x);
            const f32 maxH = Parent->Bounds.height - (Bounds.y + Transform.Translation.y);
            if (maxW > 140.0f)
            {
                m_width = Min(m_width, maxW);
            }
            if (maxH > 70.0f)
            {
                m_height = Min(m_height, maxH);
            }
        }
        ApplySize();
    }

    void FloatingPanel::ApplySize()
    {
        if (!LayoutParams)
        {
            return;
        }
        LayoutParams->Width =
            (m_width > 0.0f) ? ui::SizeSpec::Fixed(ui::Unit::Dp(m_width)) : ui::SizeSpec::Wrap();
        LayoutParams->Height = (m_height > 0.0f && !m_collapsed)
                                   ? ui::SizeSpec::Fixed(ui::Unit::Dp(m_height))
                                   : ui::SizeSpec::Wrap();
        Invalidate();
    }

    void FloatingPanel::ClampToParent()
    {
        if (Parent == nullptr)
        {
            return;
        }
        // Effective rect = Bounds (laid-out) + Transform.Translation; keep it within [0, parentSize].
        const f32 pw = Parent->Bounds.width;
        const f32 ph = Parent->Bounds.height;
        f32 minTx = -Bounds.x;
        f32 maxTx = pw - Bounds.width - Bounds.x;
        f32 minTy = -Bounds.y;
        f32 maxTy = ph - Bounds.height - Bounds.y;
        if (maxTx < minTx) { maxTx = minTx; } // panel wider than the parent: pin to the left
        if (maxTy < minTy) { maxTy = minTy; }
        Transform.Translation.x = Clamp(Transform.Translation.x, minTx, maxTx);
        Transform.Translation.y = Clamp(Transform.Translation.y, minTy, maxTy);
    }
}
