// Editor App - :container_list_editor partition
//
// A generic list-of-asset-slots PropertyEditor: ONE property-grid row whose editor view is a header
// (add icon, top right) over a column of slot rows - each an AssetPickerSlot that fills + move-up /
// move-down / remove icon buttons. Fully callback-driven with NO reflection or component coupling: the
// consumer wires OnAdd / OnPickSlot / OnRemoveSlot / OnMoveSlot and sets slotNames before the row
// builds. Shared so the scene inspector's material + reflected-container lists AND bespoke asset pages
// (e.g. the particle effect page's per-submesh material list) compose the identical widget.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:container_list_editor;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import :editor_icons;
import :asset_picker_slot;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class ContainerListEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(ContainerListEditor, ui::toolkit::PropertyEditor)
    public:
        Function<void(usize)> OnPickSlot;       // pick/assign the asset in slot i
        Function<void(usize)> OnRemoveSlot;      // remove slot i
        Function<void(usize, bool)> OnMoveSlot;  // reorder slot i (true = up)
        Function<void()> OnAdd;                  // append a new (empty) slot
        Array<String> slotNames;                 // per-slot display text, set before the row builds

        ContainerListEditor(StringView name, StringView category)
            : ui::toolkit::PropertyEditor(name, category)
        {
        }

        void RefreshView() override {}

    protected:
        RefPtr<ui::View> CreateEditorView() override;
    };
}
