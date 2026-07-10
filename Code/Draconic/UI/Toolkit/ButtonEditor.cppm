// Draconic UI Toolkit - :button_editor partition
//
// Property editor that displays a clickable Button (used for actions like "Add Condition"). Ported from
// Sedulous.UI.Toolkit/src/PropertyGrid/ButtonEditor.bf. Beef `delegate void() Action` -> Function<void()>;
// `new Button(Name)` -> a RefPtr<Button> returned as the editor view.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui.toolkit:button_editor;

import draconic.core;
import draconic.vg;
import draconic.ui;
import :property_editor;

using namespace draconic::core;

export namespace draconic::ui::toolkit
{
    /// Property editor that displays a clickable button.
    class ButtonEditor : public PropertyEditor
    {
        DRACONIC_OBJECT(ButtonEditor, PropertyEditor)
    public:
        Function<void()> Action;

        ButtonEditor(StringView name, Function<void()> action, StringView category = {})
            : PropertyEditor(name, category), Action(Move(action))
        {
        }

        void RefreshView() override {}

    protected:
        RefPtr<View> CreateEditorView() override
        {
            RefPtr<Button> btn = MakeRef<Button>(DefaultAllocator(), Name());
            ButtonEditor* self = this;
            btn->OnClick.Add([self](ButtonBase*) { if (self->Action) { self->Action(); } });
            return btn;
        }
    };

    DRACONIC_DEFINE_OBJECT(ButtonEditor, "draconic::ui::toolkit")
}
