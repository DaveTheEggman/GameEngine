// UI - :expander partition
//
// Collapsible container with a clickable header and expandable body. Ported from
// Sedulous.UI/src/Controls/Expander.bf; the header is now a STRUCTURED band (ExpanderHeader child
// view) instead of ad hoc draw + a manually-placed actions view: the band grows to fit oversized
// action widgets so they do not overflow the fixed HeaderHeight and clip when collapsed, the
// title reserves width so it cannot run under the actions,
// and header hover is the band's own hover, not the whole expander's.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:expander;

import foundation.core;
import foundation.vg;
import foundation.fonts; // CachedFont, TextAlignment, VerticalAlignment
import :view;
import :event;
import :property;
import :control_state;
import :style_property;
import :thickness;
import :box_constraints;
import :draw_context;
import :drawable;
import :event_args;
import :input_enums;
import :enums;

using namespace foundation::core;
namespace fonts = foundation::fonts;

export namespace foundation::ui
{
    class Expander;

    /// The Expander's header band: a real layout row (chevron + title drawn from the OWNER's
    /// styling parts, actions as a right-aligned child). Height = max(owner HeaderHeight,
    /// actions + padding) so oversized actions grow the band instead of overflowing it.
    /// Clicking the band toggles the owner; clicks the actions handle never reach the toggle.
    class ExpanderHeader : public ViewGroup
    {
        RTTI_OBJECT(ExpanderHeader, ViewGroup)
    public:
        explicit ExpanderHeader(Expander* owner) : m_owner(owner) { Cursor = CursorType::Hand; }

        /// Replace the right-aligned actions view (null clears).
        void SetActions(View* actions, LayoutParamsPtr lp = {})
        {
            if (m_actions != nullptr)
            {
                RemoveView(m_actions, true);
            }
            m_actions = actions;
            if (actions != nullptr)
            {
                AddView(actions, Move(lp));
            }
        }

        void OnMouseDown(MouseEventArgs& e) override;
        void OnDraw(UIDrawContext& ctx) override;

    protected:
        void OnMeasure(BoxConstraints constraints) override;
        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            if (m_actions != nullptr && m_actions->Visibility != Visibility::Gone)
            {
                const f32 aw = m_actions->MeasuredSize.x;
                const f32 ah = m_actions->MeasuredSize.y;
                m_actions->Layout(Max(0.0f, width - aw - kActionsPad),
                                  Max(0.0f, (height - ah) * 0.5f), aw, ah);
            }
        }

    private:
        friend class Expander;
        static constexpr f32 kChevronSize = 8.0f;
        static constexpr f32 kChevronX = 8.0f;
        static constexpr f32 kTextGap = 8.0f;
        static constexpr f32 kActionsPad = 4.0f; // right inset for the actions block
        static constexpr f32 kActionsVPad = 2.0f; // band padding when actions set its height

        /// Width the title must not paint into (actions block + its insets), 0 when none.
        [[nodiscard]] f32 ReservedActionsWidth() const
        {
            if (m_actions == nullptr || m_actions->Visibility == Visibility::Gone)
            {
                return 0.0f;
            }
            return m_actions->MeasuredSize.x + kActionsPad * 2.0f;
        }

        Expander* m_owner = nullptr; // the styled control the band draws for (never null)
        View* m_actions = nullptr;   // right-aligned header widgets; owned by m_children
    };

    class Expander : public ViewGroup
    {
        RTTI_OBJECT(Expander, ViewGroup)
    public:
        /// MINIMUM header band height - the band grows beyond it to fit oversized actions.
        Property<f32> HeaderHeight{28.0f};
        Property<f32> ContentSpacing{4.0f};
        Event<void(Expander*, bool)> OnExpandedChanged;

        Expander()
        {
            IsFocusable = true;
            HeaderHeight.SetOwner(this);
            ContentSpacing.SetOwner(this);
            RefPtr<ExpanderHeader> header = MakeRef<ExpanderHeader>(DefaultAllocator(), this);
            m_header = header.Get();
            AddView(header.Get());
        }
        explicit Expander(StringView headerText) : Expander() { m_headerText = String(headerText); }

        [[nodiscard]] bool IsExpanded() const noexcept { return m_isExpanded; }
        void SetIsExpanded(bool value)
        {
            if (m_isExpanded == value)
            {
                return;
            }
            m_isExpanded = value;
            if (m_content != nullptr)
            {
                m_content->Visibility = m_isExpanded ? Visibility::Visible : Visibility::Gone;
            }
            Invalidate();
            OnExpandedChanged.Invoke(this, m_isExpanded);
        }

        /// Set the expandable body content.
        void SetContent(View* content, LayoutParamsPtr lp = {})
        {
            if (m_content != nullptr)
            {
                RemoveView(m_content, true);
            }
            m_content = content;
            if (content != nullptr)
            {
                content->Visibility = m_isExpanded ? Visibility::Visible : Visibility::Gone;
                AddView(content, Move(lp));
            }
        }

        /// Right-aligned action widgets in the header band (e.g. copy / remove icon buttons). A
        /// click an action handles never reaches the band's toggle; clicking elsewhere on the
        /// band toggles. The band grows to fit the actions when they exceed HeaderHeight.
        void SetHeaderActions(View* actions, LayoutParamsPtr lp = {})
        {
            m_header->SetActions(actions, Move(lp));
        }

        void Toggle() { SetIsExpanded(!m_isExpanded); }
        void Expand() { SetIsExpanded(true); }
        void Collapse() { SetIsExpanded(false); }
        [[nodiscard]] StringView HeaderText() const { return m_headerText.AsView(); }
        void SetHeaderText(StringView text)
        {
            m_headerText = String(text);
            Invalidate();
        }
        /// The header band's laid-out height (>= HeaderHeight once measured).
        [[nodiscard]] f32 HeaderBandHeight() const { return m_header->MeasuredSize.y; }

        void OnKeyDown(KeyEventArgs& e) override
        {
            if (!IsEffectivelyEnabled())
            {
                return;
            }
            switch (e.Key)
            {
            case KeyCode::Space:
            case KeyCode::Return:
                Toggle();
                e.Handled = true;
                break;
            case KeyCode::Right:
                if (!m_isExpanded)
                {
                    SetIsExpanded(true);
                    e.Handled = true;
                }
                break;
            case KeyCode::Left:
                if (m_isExpanded)
                {
                    SetIsExpanded(false);
                    e.Handled = true;
                }
                break;
            default:
                break;
            }
        }
        void OnActivate() override { Toggle(); }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            m_header->Measure(constraints.Loosen());
            const f32 bandH = m_header->MeasuredSize.y;
            f32 contentH = 0;
            if (m_content != nullptr && m_content->Visibility != Visibility::Gone)
            {
                const BoxConstraints inner = constraints.Deflate(Padding).Loosen();
                const Thickness margin =
                    m_content->LayoutParams ? m_content->LayoutParams->Margin : Thickness{};
                m_content->Measure(inner.Deflate(margin));
                contentH =
                    ContentSpacing.Value() + m_content->MeasuredSize.y + margin.TotalVertical();
            }
            // Bounded fill (P2c): default width under an unbounded parent.
            MeasuredSize = Float2{constraints.ConstrainWidth(constraints.BoundedMaxWidth(200.0f)),
                                  constraints.ConstrainHeight(bandH + contentH)};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            const f32 bandH = m_header->MeasuredSize.y;
            m_header->Layout(0, 0, width, bandH);
            if (m_content != nullptr && m_content->Visibility != Visibility::Gone)
            {
                const Thickness margin =
                    m_content->LayoutParams ? m_content->LayoutParams->Margin : Thickness{};
                const f32 contentTop = bandH + ContentSpacing.Value();
                m_content->Layout(margin.Left, contentTop + margin.Top,
                                  Max(0.0f, width - margin.TotalHorizontal()),
                                  Max(0.0f, height - contentTop - margin.TotalVertical()));
            }
        }

    private:
        friend class ExpanderHeader;
        String m_headerText{};
        ExpanderHeader* m_header = nullptr; // the structured band; owned by m_children
        View* m_content = nullptr;          // owned by m_children (via AddView)
        bool m_isExpanded = true;
    };

    // === ExpanderHeader bodies (need the complete Expander type) ===

    inline void ExpanderHeader::OnMouseDown(MouseEventArgs& e)
    {
        if (!IsEffectivelyEnabled() || e.Handled || e.Button != MouseButton::Left)
        {
            return;
        }
        m_owner->Toggle();
        e.Handled = true;
    }

    inline void ExpanderHeader::OnMeasure(BoxConstraints constraints)
    {
        f32 bandH = m_owner->HeaderHeight.Value();
        if (m_actions != nullptr && m_actions->Visibility != Visibility::Gone)
        {
            m_actions->Measure(constraints.Loosen());
            bandH = Max(bandH, m_actions->MeasuredSize.y + kActionsVPad * 2.0f);
        }
        MeasuredSize = Float2{constraints.ConstrainWidth(constraints.BoundedMaxWidth(200.0f)),
                              constraints.ConstrainHeight(bandH)};
    }

    inline void ExpanderHeader::OnDraw(UIDrawContext& ctx)
    {
        // Band background from the OWNER's ::header part; hover is the band's own hover (the
        // sheets' Expander::header:hover means "pointer over the header", not over the body).
        const Rectangle band{0, 0, Width(), Height()};
        ControlState bandState = GetControlState();
        if (!m_owner->IsEffectivelyEnabled())
        {
            bandState |= ControlState::Disabled;
        }
        if (Drawable* header =
                m_owner->ResolvePartDrawable(u8"header", StyleProperty::Background, bandState))
        {
            header->Draw(ctx, band, bandState);
        }
        else
        {
            ctx.VG().FillRect(band, Color{50.0f / 255.0f, 55.0f / 255.0f, 68.0f / 255.0f, 1.0f});
        }

        const f32 cy = Height() * 0.5f;
        ControlState chevronState = bandState;
        if (m_owner->IsExpanded())
        {
            chevronState |= ControlState::Checked;
        }
        const Color chevronColor =
            m_owner->ResolvePartColor(u8"chevron", StyleProperty::TextColor, chevronState,
                                      Color{180.0f / 255.0f, 185.0f / 255.0f, 200.0f / 255.0f, 1.0f});
        if (Drawable* chevron =
                m_owner->ResolvePartDrawable(u8"chevron", StyleProperty::Background, chevronState))
        {
            chevron->Draw(ctx, Rectangle{kChevronX, cy - kChevronSize * 0.5f, kChevronSize,
                                         kChevronSize});
        }
        else
        {
            ctx.VG().BeginPath();
            if (m_owner->IsExpanded())
            {
                ctx.VG().MoveTo(kChevronX, cy - kChevronSize * 0.25f);
                ctx.VG().LineTo(kChevronX + kChevronSize * 0.5f, cy + kChevronSize * 0.25f);
                ctx.VG().LineTo(kChevronX + kChevronSize, cy - kChevronSize * 0.25f);
            }
            else
            {
                ctx.VG().MoveTo(kChevronX + kChevronSize * 0.25f, cy - kChevronSize * 0.5f);
                ctx.VG().LineTo(kChevronX + kChevronSize * 0.75f, cy);
                ctx.VG().LineTo(kChevronX + kChevronSize * 0.25f, cy + kChevronSize * 0.5f);
            }
            ctx.VG().Stroke(chevronColor, 2.0f);
        }

        const StringView title = m_owner->HeaderText();
        if (title.Size() > 0 && ctx.FontService() != nullptr)
        {
            const f32 fontSize = m_owner->ResolveStyleFloat(StyleProperty::FontSize, 16.0f);
            if (fonts::CachedFont* font =
                    ctx.FontService()->GetFont(m_owner->ResolveStyleFontFamily(), fontSize))
            {
                const Color textColor = m_owner->ResolveStyleColor(
                    StyleProperty::TextColor,
                    Color{220.0f / 255.0f, 225.0f / 255.0f, 235.0f / 255.0f, 1.0f});
                // The title's rect ends where the actions block begins - it can no longer run
                // underneath the buttons.
                const f32 textX = kChevronX + kChevronSize + kTextGap;
                const f32 textW = Max(0.0f, Width() - textX - ReservedActionsWidth() - 4.0f);
                ctx.VG().DrawText(title, font, Rectangle{textX, 0, textW, Height()},
                                  fonts::TextAlignment::Left, fonts::VerticalAlignment::Middle,
                                  textColor);
            }
        }
        DrawChildren(ctx);
    }

    RTTI_DEFINE_OBJECT(ExpanderHeader, "rtti::ui")
    RTTI_DEFINE_OBJECT(Expander, "rtti::ui")
}
