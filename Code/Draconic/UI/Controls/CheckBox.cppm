// Draconic UI - :checkbox partition
//
// Toggle checkbox with a text label. Ported from Sedulous.UI/src/Controls/CheckBox.bf (a View, not a
// ToggleButton). Text measuring/drawing is deferred (guarded by the null Fonts service); the box glyph
// rendering (part drawables + fallback path) is deferred with it. Toggle/state/event logic is faithful.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:checkbox;

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
    class CheckBox : public View
    {
        DRACONIC_OBJECT(CheckBox, View)
    public:
        Property<bool> IsChecked{ false };
        Property<String> Text;
        Property<Optional<f32>> FontSize;
        Property<String> FontFamily;
        Property<Optional<core::Color>> TextColor;
        Event<void(CheckBox*, bool)> OnCheckedChanged;

        CheckBox() { Init(); }
        explicit CheckBox(StringView text) { Init(); Text.SetSilent(String(text)); }
        CheckBox(StringView text, bool isChecked) { Init(); Text.SetSilent(String(text)); IsChecked.SetSilent(isChecked); }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }
            if (e.Button == MouseButton::Left) { IsChecked.SetValue(!IsChecked.Value()); e.Handled = true; }
        }
        void OnKeyDown(KeyEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }
            if (e.Key == KeyCode::Space || e.Key == KeyCode::Return) { IsChecked.SetValue(!IsChecked.Value()); e.Handled = true; }
        }
        void OnActivate() override { if (IsEffectivelyEnabled()) { IsChecked.SetValue(!IsChecked.Value()); } }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const f32 boxSize = ResolvePartFloat(u8"box", StyleProperty::Width, GetControlState(), 18.0f);
            // Text measuring deferred: textW/textH = 0, so total collapses to the box size.
            MeasuredSize = Float2{ constraints.ConstrainWidth(boxSize), constraints.ConstrainHeight(boxSize) };
        }
        void OnDraw(UIDrawContext& ctx) override
        {
            // Box + checkmark + label rendering deferred until the Fonts service + draw path are wired.
            (void)ctx;
        }

    private:
        void Init()
        {
            IsChecked.SetOwner(this, InvalidationKind::Visual);
            Text.SetOwner(this);
            FontSize.SetOwner(this);
            FontFamily.SetOwner(this, InvalidationKind::Visual);
            TextColor.SetOwner(this, InvalidationKind::Visual);
            CheckBox* self = this;
            IsChecked.Changed.Add(Event<void(bool)>::Handler{ [self](bool val) { self->OnCheckedChanged.Invoke(self, val); } });
            IsFocusable = true;
            IsTabStop = true;
            Cursor = CursorType::Hand;
        }
    };

    DRACONIC_DEFINE_OBJECT(CheckBox, "draconic::ui")
}
