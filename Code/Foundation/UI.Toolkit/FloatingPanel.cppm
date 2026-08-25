// UI.Toolkit - :floating_panel partition.
//
// A floating panel that hosts arbitrary content OVER another view (NOT an OS window and NOT
// docking-managed - that is the DockableWindow / IDockableWindowHost world). It is a self-contained
// tool window: a themed title bar (title + collapse + close icon buttons), a content region, and a
// bottom-right resize grip. Chrome quality follows DockablePanel; the resize follows SplitView's
// pattern (an ABSOLUTE pointer->size mapping stored in one authoritative field, never accumulated
// deltas, so there is no size-fighting/jitter). Content fills the body, so resizing the panel
// resizes its contents.
//
// Positioning: the panel reports its own size via MeasuredSize and is placed by its parent (a
// FrameLayout) with a corner gravity; a header drag offsets it via Transform.Translation, clamped so
// it can't leave the parent (a sibling pane would draw over it and steal input).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.toolkit:floating_panel;

import foundation.core;
import foundation.vg;
import foundation.ui;
import foundation.fonts;

using namespace foundation::core;

export namespace foundation::ui::toolkit
{
    class FloatingPanel : public ViewGroup
    {
        RTTI_OBJECT(FloatingPanel, ViewGroup)
    public:
        explicit FloatingPanel(StringView title) : m_title(title)
        {
            // Own the icon drawables (live-rendered SVG; IconButton tints per-draw). Kept as members
            // so the borrowed pointers handed to the buttons outlive them.
            m_closeIcon = SVGDrawable::FromString(ThemeIcons::Close());
            m_chevronDown = SVGDrawable::FromString(ThemeIcons::ChevronDown());
            m_chevronRight = SVGDrawable::FromString(ThemeIcons::ChevronRight());

            m_collapseBtn = MakeRef<IconButton>(DefaultAllocator(), m_chevronDown.Get(), kIconSize);
            m_collapseBtn->AddClass(u8"floatingpanel-header-button");
            m_collapseBtn->OnClick.Add([this](ButtonBase*) { SetCollapsed(!m_collapsed); });
            AddView(m_collapseBtn.Get());

            m_closeBtn = MakeRef<IconButton>(DefaultAllocator(), m_closeIcon.Get(), kIconSize);
            m_closeBtn->AddClass(u8"floatingpanel-close-button");
            m_closeBtn->OnClick.Add([this](ButtonBase*) { OnClose.Invoke(); });
            AddView(m_closeBtn.Get());
        }

        /// Replace the body content (below the title bar). Null clears it.
        void SetContent(RefPtr<View> content)
        {
            if (m_content)
            {
                RemoveView(m_content.Get(), true);
            }
            m_content = Move(content);
            if (m_content)
            {
                AddView(m_content.Get());
                m_content->Visibility = m_collapsed ? Visibility::Gone : Visibility::Visible;
            }
            Invalidate();
        }

        void SetTitle(StringView title)
        {
            m_title = String(title);
            Invalidate();
        }

        /// The panel's initial/target size expressed as the desired CONTENT size (title bar and insets
        /// are added). Clamped to the panel minimum. Resizing overrides this afterward.
        void SetPreferredContentSize(f32 width, f32 height)
        {
            m_userW = Max(kMinWidth, width + 2.0f * kContentInset);
            m_userH = Max(kMinHeight, kHeaderHeight + height + kContentInset);
            Invalidate();
        }

        void SetCollapsed(bool collapsed)
        {
            if (m_collapsed == collapsed)
            {
                return;
            }
            m_collapsed = collapsed;
            m_collapseBtn->SetIcon(collapsed ? m_chevronRight.Get() : m_chevronDown.Get());
            if (m_content)
            {
                m_content->Visibility = collapsed ? Visibility::Gone : Visibility::Visible;
            }
            Invalidate();
        }
        [[nodiscard]] bool IsCollapsed() const noexcept { return m_collapsed; }

        /// Fired by the close button. The consumer decides the effect (e.g. deactivate the tool).
        Event<void()> OnClose;

    protected:
        // The panel is authoritatively sized by m_userW/m_userH (clamped to the panel min and the
        // parent's available room). Content is a consequence of that size, never an input to it - the
        // one-way flow that keeps resize stable.
        void OnMeasure(BoxConstraints constraints) override
        {
            const f32 w = constraints.ConstrainWidth(Max(kMinWidth, m_userW));
            const f32 h = m_collapsed ? constraints.ConstrainHeight(kHeaderHeight)
                                      : constraints.ConstrainHeight(Max(kMinHeight, m_userH));
            // Measure children against the box they'll occupy (buttons tight; content fills the body).
            m_collapseBtn->Measure(BoxConstraints::Tight(kIconSize, kIconSize));
            m_closeBtn->Measure(BoxConstraints::Tight(kIconSize, kIconSize));
            if (m_content && !m_collapsed)
            {
                const f32 bodyW = Max(0.0f, w - 2.0f * kContentInset);
                const f32 bodyH = Max(0.0f, h - kHeaderHeight - kContentInset);
                m_content->Measure(BoxConstraints::Tight(bodyW, bodyH));
            }
            MeasuredSize = Float2{w, h};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            // Title-bar buttons: close at the right edge, collapse to its left, vertically centred.
            const f32 btnY = (kHeaderHeight - kIconSize) * 0.5f;
            const f32 closeX = width - kContentInset - kIconSize;
            const f32 collapseX = closeX - kIconSize - 2.0f;
            m_closeBtn->Layout(closeX, btnY, kIconSize, kIconSize);
            m_collapseBtn->Layout(collapseX, btnY, kIconSize, kIconSize);

            if (m_content && !m_collapsed)
            {
                const f32 bodyW = Max(0.0f, width - 2.0f * kContentInset);
                const f32 bodyH = Max(0.0f, height - kHeaderHeight - kContentInset);
                m_content->Layout(kContentInset, kHeaderHeight, bodyW, bodyH);
            }
        }

        void OnDraw(UIDrawContext& ctx) override
        {
            const f32 w = Width();
            const f32 h = Height();

            // Panel body background + border.
            if (Drawable* bg = ResolveStyleDrawable(StyleProperty::Background))
            {
                bg->Draw(ctx, Rectangle{0, 0, w, h});
            }
            else
            {
                ctx.VG().FillRect(Rectangle{0, 0, w, h}, Color{0.149f, 0.157f, 0.196f, 0.961f});
            }
            const Color border = ResolveStyleColor(StyleProperty::BorderColor, Color{0.255f, 0.275f, 0.333f, 1.0f});
            ctx.VG().StrokeRect(Rectangle{0, 0, w, h}, border, 1.0f);

            // Title bar background.
            if (Drawable* headerBg =
                    ResolvePartDrawable(u8"header", StyleProperty::Background, ControlState::Normal))
            {
                headerBg->Draw(ctx, Rectangle{0, 0, w, kHeaderHeight});
            }
            else
            {
                ctx.VG().FillRect(Rectangle{0, 0, w, kHeaderHeight}, Color{0.188f, 0.204f, 0.251f, 1.0f});
            }

            // Title text (left-aligned, clipped to leave room for the two buttons).
            if (ctx.FontService() != nullptr)
            {
                const f32 fontSize = ResolveStyleFloat(StyleProperty::FontSize, 12.0f);
                if (fonts::CachedFont* font = ctx.FontService()->GetFont(fontSize))
                {
                    const Color textColor =
                        ResolveStyleColor(StyleProperty::TextColor, Color{0.863f, 0.882f, 0.922f, 1.0f});
                    const f32 textW = Max(0.0f, w - (2.0f * kIconSize) - 3.0f * kContentInset);
                    ctx.VG().DrawText(m_title.AsView(), font,
                                      Rectangle{kContentInset + 2.0f, 0, textW, kHeaderHeight},
                                      fonts::TextAlignment::Left, fonts::VerticalAlignment::Middle,
                                      textColor);
                }
            }

            DrawChildren(ctx);

            // Bottom-right resize grip affordance (three short diagonals), hidden when collapsed.
            if (!m_collapsed)
            {
                const Color grip = ResolveStyleColor(StyleProperty::BorderColor, Color{0.471f, 0.494f, 0.549f, 0.784f});
                for (i32 i = 1; i <= 3; ++i)
                {
                    const f32 o = static_cast<f32>(i) * 4.0f;
                    ctx.VG().DrawLine(Float2{w - o, h - 2.0f}, Float2{w - 2.0f, h - o}, grip, 1.0f);
                }
            }
        }

        // Intercept the resize band before children so the grip works even over content.
        [[nodiscard]] View* HitTest(Float2 p) override
        {
            if (Visibility == Visibility::Visible && IsInteractionEnabled && !m_collapsed &&
                p.x >= 0 && p.y >= 0 && p.x < Width() && p.y < Height())
            {
                bool right = false, bottom = false;
                if (InResizeBand(p, right, bottom))
                {
                    return this;
                }
            }
            return ViewGroup::HitTest(p);
        }

        [[nodiscard]] CursorType CursorAt(Float2 p) const override
        {
            if (!m_collapsed)
            {
                bool right = false, bottom = false;
                if (InResizeBand(p, right, bottom))
                {
                    if (right && bottom) return CursorType::SizeNWSE;
                    if (right) return CursorType::SizeWE;
                    return CursorType::SizeNS;
                }
            }
            // Header drag zone (left of the buttons).
            if (p.y < kHeaderHeight && p.x < Width() - 2.0f * kIconSize - 3.0f * kContentInset)
            {
                return CursorType::Move;
            }
            return CursorType::Default;
        }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (e.Button != MouseButton::Left)
            {
                return;
            }
            bool right = false, bottom = false;
            if (!m_collapsed && InResizeBand(Float2{e.X, e.Y}, right, bottom))
            {
                m_resizing = true;
                m_resizeRight = right;
                m_resizeBottom = bottom;
                m_grabOffX = Width() - e.X;   // keep the grabbed point under the corner during drag
                m_grabOffY = Height() - e.Y;
                Capture();
                e.Handled = true;
                return;
            }
            if (e.Y < kHeaderHeight) // header band (buttons already consumed their own hits)
            {
                m_dragging = true;
                m_lastX = e.X;
                m_lastY = e.Y;
                Capture();
                e.Handled = true;
            }
        }

        void OnMouseMove(MouseEventArgs& e) override
        {
            if (m_resizing)
            {
                if (m_resizeRight)
                {
                    m_userW = Clamp(e.X + m_grabOffX, kMinWidth, MaxWidthInParent());
                }
                if (m_resizeBottom)
                {
                    m_userH = Clamp(e.Y + m_grabOffY, kMinHeight, MaxHeightInParent());
                }
                Invalidate();
                e.Handled = true;
            }
            else if (m_dragging)
            {
                Transform.Translation.x += (e.X - m_lastX);
                Transform.Translation.y += (e.Y - m_lastY);
                m_lastX = e.X;
                m_lastY = e.Y;
                ClampToParent();
                e.Handled = true;
            }
        }

        void OnMouseUp(MouseEventArgs& e) override
        {
            if (e.Button == MouseButton::Left && (m_resizing || m_dragging))
            {
                m_resizing = false;
                m_dragging = false;
                if (Context != nullptr)
                {
                    Context->GetFocusManager()->ReleaseCapture();
                }
                e.Handled = true;
            }
        }

    private:
        static constexpr f32 kHeaderHeight = 26.0f;
        static constexpr f32 kContentInset = 6.0f;
        static constexpr f32 kResizeBand = 9.0f;
        static constexpr f32 kIconSize = 18.0f;
        static constexpr f32 kMinWidth = 160.0f;
        static constexpr f32 kMinHeight = kHeaderHeight + 60.0f;

        void Capture()
        {
            if (Context != nullptr)
            {
                Context->GetFocusManager()->SetCapture(this);
            }
        }

        [[nodiscard]] bool InResizeBand(Float2 p, bool& right, bool& bottom) const
        {
            right = p.x >= Width() - kResizeBand;
            bottom = p.y >= Height() - kResizeBand;
            return (right || bottom) && p.y > kHeaderHeight;
        }

        [[nodiscard]] f32 MaxWidthInParent() const
        {
            if (Parent == nullptr)
            {
                return Max(kMinWidth, m_userW);
            }
            return Max(kMinWidth, Parent->Bounds.width - (Bounds.x + Transform.Translation.x));
        }
        [[nodiscard]] f32 MaxHeightInParent() const
        {
            if (Parent == nullptr)
            {
                return Max(kMinHeight, m_userH);
            }
            return Max(kMinHeight, Parent->Bounds.height - (Bounds.y + Transform.Translation.y));
        }

        // Keep the panel fully inside its parent: effective rect = Bounds + Translation clamped into
        // [0, parentSize].
        void ClampToParent()
        {
            if (Parent == nullptr)
            {
                return;
            }
            const f32 pw = Parent->Bounds.width;
            const f32 ph = Parent->Bounds.height;
            f32 minTx = -Bounds.x;
            f32 maxTx = pw - Bounds.width - Bounds.x;
            f32 minTy = -Bounds.y;
            f32 maxTy = ph - Bounds.height - Bounds.y;
            if (maxTx < minTx) { maxTx = minTx; }
            if (maxTy < minTy) { maxTy = minTy; }
            Transform.Translation.x = Clamp(Transform.Translation.x, minTx, maxTx);
            Transform.Translation.y = Clamp(Transform.Translation.y, minTy, maxTy);
        }

        String m_title;
        RefPtr<SVGDrawable> m_closeIcon;
        RefPtr<SVGDrawable> m_chevronDown;
        RefPtr<SVGDrawable> m_chevronRight;
        RefPtr<IconButton> m_collapseBtn;
        RefPtr<IconButton> m_closeBtn;
        RefPtr<View> m_content;

        bool m_collapsed = false;
        f32 m_userW = 240.0f;                 // authoritative panel size (content-driven default)
        f32 m_userH = kHeaderHeight + 170.0f;

        bool m_dragging = false;
        bool m_resizing = false;
        bool m_resizeRight = false;
        bool m_resizeBottom = false;
        f32 m_lastX = 0.0f;
        f32 m_lastY = 0.0f;
        f32 m_grabOffX = 0.0f;
        f32 m_grabOffY = 0.0f;
    };

    RTTI_DEFINE_OBJECT(FloatingPanel, "rtti::ui::toolkit")
}
