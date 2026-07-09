// Draconic UI - :radio_button partition
//
// Radio button (cannot be unchecked by click; use RadioGroup for mutual exclusion). Ported from
// Sedulous.UI/src/Controls/RadioButton.bf (a View). Text render deferred; toggle/state/event faithful.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:radio_button;

import draconic.core;
import :view;
import :event;
import :property;
import :control_state;
import :style_property;
import :box_constraints;
import :draw_context;
import :event_args;
import :input_enums;
import :enums;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::ui
{
    class RadioButton : public View
    {
        DRACONIC_OBJECT(RadioButton, View)
    public:
        Property<bool> IsChecked{ false };
        Property<String> Text;
        Property<Optional<f32>> FontSize;
        Property<String> FontFamily;
        Property<Optional<core::Color>> TextColor;
        Event<void(RadioButton*, bool)> OnCheckedChanged;

        RadioButton() { Init(); }
        explicit RadioButton(StringView text) { Init(); Text.SetSilent(String(text)); }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }
            if (e.Button == MouseButton::Left && !IsChecked.Value()) { IsChecked.SetValue(true); e.Handled = true; }
        }
        void OnKeyDown(KeyEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }
            if ((e.Key == KeyCode::Space || e.Key == KeyCode::Return) && !IsChecked.Value()) { IsChecked.SetValue(true); e.Handled = true; }
        }
        void OnActivate() override { if (IsEffectivelyEnabled()) { IsChecked.SetValue(true); } }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            // Text measuring deferred: total collapses to the circle size.
            MeasuredSize = Float2{ constraints.ConstrainWidth(kCircleSize), constraints.ConstrainHeight(kCircleSize) };
        }
        void OnDraw(UIDrawContext& ctx) override { (void)ctx; } // circle + text render deferred

    private:
        static constexpr f32 kCircleSize = 18.0f;

        void Init()
        {
            IsChecked.SetOwner(this, InvalidationKind::Visual);
            Text.SetOwner(this);
            FontSize.SetOwner(this);
            FontFamily.SetOwner(this, InvalidationKind::Visual);
            TextColor.SetOwner(this, InvalidationKind::Visual);
            RadioButton* self = this;
            IsChecked.Changed.Add(Event<void(bool)>::Handler{ [self](bool val) { self->OnCheckedChanged.Invoke(self, val); } });
            IsFocusable = true; IsTabStop = true; Cursor = CursorType::Hand;
        }
    };

    DRACONIC_DEFINE_OBJECT(RadioButton, "draconic::ui")
}
