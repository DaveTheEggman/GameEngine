// Draconic UI Toolkit - :property_editor partition
//
// Abstract base for the typed property editors used by PropertyGrid (label + editing-control row).
// Ported from Sedulous.UI.Toolkit/src/PropertyGrid/PropertyEditor.bf. Supports transactional editing for
// undo/redo integration: OnEditBegin / OnValueChanged (may fire many times) / OnEditEnd / OnEditCancelled.
//
// Beef `abstract class PropertyEditor` (implicitly Object-derived) -> `Object` + DRACONIC_OBJECT (abstract:
// pure-virtual CreateEditorView/RefreshView, but still carries a type identity like the core abstract
// Drawable). Beef owned `View mEditorView` (owned by the view tree once added) -> a RefPtr<View> the editor
// holds so its borrowed child pointers stay valid across PropertyGrid rebuilds. `Event<delegate
// void(PropertyEditor)>` -> Event<void(PropertyEditor*)>; `delegate void(StringView) OnLabelRenamed` ->
// Function<void(StringView)>. Beef `String` fields with "null == unset" -> core::String with "empty ==
// unset".

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui.toolkit:property_editor;

import draconic.core;
import draconic.vg;
import draconic.ui;

using namespace draconic::core;

export namespace draconic::ui::toolkit
{
    /// Abstract base for typed property editors used by PropertyGrid. Subclasses implement
    /// CreateEditorView() to return the editing control and RefreshView() to update it from external state.
    class PropertyEditor : public Object
    {
        DRACONIC_OBJECT(PropertyEditor, Object)
    public:
        /// Fired each time the value changes (may fire multiple times per edit gesture).
        Event<void(PropertyEditor*)> OnValueChanged;

        /// Fired once when an edit gesture begins (drag start, text field focus, etc.).
        Event<void(PropertyEditor*)> OnEditBegin;

        /// Fired once when an edit gesture completes successfully.
        Event<void(PropertyEditor*)> OnEditEnd;

        /// Fired if an edit gesture is cancelled (Escape key, etc.).
        Event<void(PropertyEditor*)> OnEditCancelled;

        /// Optional callback for making the label editable. When set, PropertyGrid renders the label as an
        /// EditText. The delegate receives the new name.
        Function<void(StringView)> OnLabelRenamed;

        explicit PropertyEditor(StringView name, StringView category = {})
            : m_name(name)
        {
            if (category.Size() > 0) { m_category = String(category); }
        }

        ~PropertyEditor() override = default;

        /// Property identity name. Stable, machine-readable (e.g., "CastsShadows").
        [[nodiscard]] StringView Name() const { return m_name; }

        /// Display label shown in the inspector UI. Falls back to Name if not set.
        [[nodiscard]] StringView DisplayName() const { return m_displayName.IsEmpty() ? StringView(m_name) : StringView(m_displayName); }

        /// Sets the display label (for pretty names like "Casts Shadows").
        void SetDisplayName(StringView displayName) { m_displayName = String(displayName); }

        [[nodiscard]] StringView Category() const { return m_category; }

        /// Whether an edit gesture is currently in progress.
        [[nodiscard]] bool IsEditing() const noexcept { return m_isEditing; }

        /// Get or create the editor view (lazy).
        [[nodiscard]] View* EditorView()
        {
            if (m_editorView.Get() == nullptr) { m_editorView = CreateEditorView(); }
            return m_editorView.Get();
        }

        /// Refresh the view from the current value (for external state changes).
        virtual void RefreshView() = 0;

    protected:
        /// Create the editing control. Called once, lazily.
        virtual RefPtr<View> CreateEditorView() = 0;

        /// Notify that the value changed (call from subclasses).
        void NotifyValueChanged() { OnValueChanged.Invoke(this); }

        /// Call when an edit gesture begins.
        void BeginEdit()
        {
            if (!m_isEditing)
            {
                m_isEditing = true;
                OnEditBegin.Invoke(this);
            }
        }

        /// Call when an edit gesture completes successfully.
        void EndEdit()
        {
            if (m_isEditing)
            {
                m_isEditing = false;
                OnEditEnd.Invoke(this);
            }
        }

        /// Call when an edit gesture is cancelled (Escape pressed, etc.).
        void CancelEdit()
        {
            if (m_isEditing)
            {
                m_isEditing = false;
                OnEditCancelled.Invoke(this);
            }
        }

    private:
        String m_name;             // machine-readable identity
        String m_displayName;      // empty == "use Name"
        String m_category;         // empty == uncategorized
        RefPtr<View> m_editorView; // owned; the view tree also refs it once added
        bool m_isEditing = false;
    };

    DRACONIC_DEFINE_OBJECT(PropertyEditor, "draconic::ui::toolkit")
}
