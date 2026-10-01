// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor App - :resource_ref_editor partition
//
// ResourceRefEditor: a property row over an AssetPickerSlot - the referenced asset's name, its
// thumbnail, and the pick, edit, clear, reveal and drop verbs. Every page uses it for a field
// that names an asset (editor-lists-and-asset-slots.md D2).
//
// The accepted asset types are a CONSTRUCTOR argument, so a row that names an asset cannot be
// built without being a drop target for that asset: AssetPickerSlot::kAnyAsset for a genuinely
// untyped field, and no types at all only for a row that is not an asset (an entity
// reference). BindAsset wires every verb to ONE assignment, so a pick, a drop and a clear are
// the same write - one code path, one undo step.
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:resource_ref_editor;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import :asset_picker_slot;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class ResourceRefEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(ResourceRefEditor, ui::toolkit::PropertyEditor)
    public:
        struct BindOptions
        {
            bool edit = true;   // the edit verb opens the bound asset
            bool reveal = true; // the reveal verb shows it in the asset browser
        };

        Function<void()> OnPick;   // opens the picker
        Function<void()> OnEdit;   // open-for-editing (EditorContext::OpenAsset routing)
        Function<void()> OnClear;  // clears the reference
        Function<void()> OnReveal; // reveal in the asset browser (EditorContext::RevealAsset)
        Function<void(const Guid&)> OnAssignDropped;           // browser drag-drop assign
        Function<void(StringView, StringView)> OnRejectedDrop; // wrong-type drop -> toast

        /// `acceptedTypes`: the asset types a pick offers and a drop takes (kAnyAsset for any;
        /// empty only for a row that names no asset). The first names the slot's type icon.
        ResourceRefEditor(StringView name, StringView valueText, StringView category,
                          Span<const StringView> acceptedTypes);

        /// Wires pick, drop, clear, edit and reveal to ONE assignment through `context`:
        /// - pick opens the asset picker filtered by the accepted types, and assigns its choice;
        /// - a drop of an accepted type is assigned, the same write; a refused one is reported;
        /// - clear assigns the nil id;
        /// - edit and reveal open and show the asset `current` names.
        /// The row shows `current` from then on (Refresh), and refreshes after every assignment;
        /// it holds itself across `assign`, so a write that rebuilds the row's grid is safe.
        /// `options` drops the edit and reveal verbs for a field whose asset is not opened from
        /// here.
        void BindAsset(editor::EditorContext& context, Function<Guid()> current,
                       Function<void(const Guid&)> assign);
        void BindAsset(editor::EditorContext& context, Function<Guid()> current,
                       Function<void(const Guid&)> assign, BindOptions options);

        /// Re-reads the bound asset: its name, and its thumbnail when the context has one.
        void Refresh();

        void SetValueText(StringView text);
        /// What a bound row shows for the nil id: "(none)", or what nil means for the field (a
        /// preview's default shape). Set before binding.
        void SetEmptyText(StringView text) { m_emptyText = String(text); }
        [[nodiscard]] StringView EmptyText() const noexcept { return m_emptyText.AsView(); }
        [[nodiscard]] StringView ValueText() const noexcept { return m_valueText.AsView(); }
        [[nodiscard]] Span<const String> AcceptedTypes() const noexcept
        {
            return Span<const String>{m_acceptedTypes.Data(), m_acceptedTypes.Size()};
        }
        /// The row's slot; null until the grid builds the row.
        [[nodiscard]] AssetPickerSlot* Slot() const noexcept { return m_slot.Get(); }

        /// The asset TYPE glyph for the slot preview (set from the first accepted type).
        void SetPreviewIcon(ui::SVGDrawable* icon) { m_previewIcon = icon; }
        /// The generated thumbnail (wins over the type icon while set; empty falls back).
        void SetPreviewThumbnail(ui::DrawablePtr thumbnail)
        {
            if (m_slot.Get() != nullptr)
            {
                m_slot->SetPreviewThumbnail(Move(thumbnail));
            }
        }

        void RefreshView() override { Refresh(); }

    protected:
        RefPtr<ui::View> CreateEditorView() override;

    private:
        void Assign(const Guid& id);

        /// A bound row has a value when its id is set (a "(missing)" one can still be cleared);
        /// an unbound row reads its text.
        [[nodiscard]] bool HasValue() const
        {
            return m_current ? m_boundHasValue : m_valueText.AsView() != StringView(u8"(none)");
        }

        String m_valueText;
        String m_emptyText{u8"(none)"};
        bool m_boundHasValue = false;
        ui::SVGDrawable* m_previewIcon = nullptr; // borrowed (EditorIcons)
        Array<String> m_acceptedTypes;
        RefPtr<AssetPickerSlot> m_slot;
        editor::EditorContext* m_context = nullptr; // set by BindAsset: names and thumbnails
        Function<Guid()> m_current;
        Function<void(const Guid&)> m_assign;
    };
}
