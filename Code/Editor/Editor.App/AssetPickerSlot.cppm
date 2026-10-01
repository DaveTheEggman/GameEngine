// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor App - :asset_picker_slot partition
//
// The asset reference "slot": a horizontal composite
//   [ asset name (grows, click = pick) ][ preview/reveal ][ Edit ][ Clear ]
// The name button IS the picker affordance (no separate
// Pick button), the preview/reveal button sits after it, and all three trailing buttons share
// ONE chrome (ContentButton hosting a 14px drawable - IconButton's tighter padding read as
// horizontally squished next to it).
// used by every resource-ref inspector row and the material list-slot rows. Affordances render
// only when their callback is WIRED (the entity-ref twin wires OnPick alone and
// degrades to a plain name button), and Edit/Clear/preview disable while the slot is empty.
// It is a type-filtered drop target for asset-browser drags; the preview icon is the
// asset TYPE glyph until a real thumbnail exists.
//
// Wiring order: set the callbacks first, then call SetValue - SetValue synchronizes the
// affordances (visibility from wiring, enabled-state from has-value).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:asset_picker_slot;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit; // TreeDragData: a hierarchy row dropped on an entity slot
import :editor_icons;
import :asset_drag_data;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class AssetPickerSlot : public ui::FlexLayout, public ui::IDropTarget
    {
        RTTI_OBJECT(AssetPickerSlot, ui::FlexLayout)
    public:
        /// Click the name body = (re)assign via the picker dialog.
        Function<void()> OnPick;
        /// Open the referenced asset for editing (EditorContext::OpenAsset routing).
        Function<void()> OnEdit;
        /// Clear the reference (must route the consumer's undoable command).
        Function<void()> OnClear;
        /// Reveal the referenced asset in the asset browser (preview click).
        Function<void()> OnReveal;
        /// A type-matching asset was dropped on the slot - assign it (the consumer's undoable
        /// command; the same write the picker takes).
        Function<void(const Guid&)> OnAssignDropped;
        /// A WRONG-TYPE asset was dropped: (asset display name, its type name). The slot already
        /// LOG_WARNINGs; wire this for the toast.
        Function<void(StringView, StringView)> OnRejectedDrop;

        explicit AssetPickerSlot(StringView name = {})
        {
            Direction = ui::Orientation::Horizontal;
            Spacing = 2.0f;
            EditorIcons& icons = EditorIcons::Get(); // null drawables pre-Initialize (tests)

            auto body = MakeRef<ui::Button>(MemoryAllocator(),
                                            name.Size() > 0 ? name : StringView(u8"(none)"));
            m_body = body.Get();
            m_body->TooltipText = String(u8"Choose asset");
            m_body->OnClick.Add(
                [this](ui::ButtonBase*)
                {
                    if (OnPick)
                    {
                        OnPick();
                    }
                });
            {
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                AddView(body.Get(), lp);
            }

            // Preview/reveal = a clickable drawable host: the asset TYPE glyph now, swapped
            // for the real thumbnail once one exists - the icon stays
            // the fallback while no thumbnail is generated.
            ui::DrawableView* previewContent = nullptr;
            m_preview = MakeActionButton(ui::DrawablePtr{}, u8"Reveal in asset browser",
                                         &previewContent);
            m_previewDrawable = previewContent;
            m_preview->OnClick.Add(
                [this](ui::ButtonBase*)
                {
                    if (OnReveal)
                    {
                        OnReveal();
                    }
                });

            m_edit = MakeActionButton(ui::DrawablePtr(icons.edit.Get()), u8"Edit asset");
            m_edit->OnClick.Add(
                [this](ui::ButtonBase*)
                {
                    if (OnEdit)
                    {
                        OnEdit();
                    }
                });

            m_clear = MakeActionButton(ui::DrawablePtr(icons.close.Get()), u8"Clear reference");
            m_clear->OnClick.Add(
                [this](ui::ButtonBase*)
                {
                    if (OnClear)
                    {
                        OnClear();
                    }
                });

            SyncAffordances(false);
        }

        /// The current reference display: name text + whether a real asset is referenced.
        /// Also synchronizes affordance visibility/enabled-state - call after wiring callbacks.
        void SetValue(StringView name, bool hasValue)
        {
            m_hasValue = hasValue;
            // The name as given, a bound row's empty text included ("Default", a preview's
            // primitive): "(none)" only stands in for no text at all.
            m_body->SetText(name.Size() > 0 ? name : StringView(u8"(none)"));
            SyncAffordances(hasValue);
        }

        /// The asset TYPE glyph for the preview (EditorIcons::ForAssetType) - the FALLBACK
        /// layer, shown whenever no thumbnail is set.
        void SetPreviewIcon(ui::SVGDrawable* icon)
        {
            m_previewIcon = icon;
            ApplyPreview();
            SyncAffordances(m_hasValue);
        }

        /// A generated thumbnail (any drawable). Wins
        /// over the type icon while set; pass empty to fall back to the icon.
        void SetPreviewThumbnail(ui::DrawablePtr thumbnail)
        {
            m_previewThumbnail = Move(thumbnail);
            ApplyPreview();
            SyncAffordances(m_hasValue);
        }

        /// Body text size passthrough (list-slot rows run compact chrome).
        void SetFontSize(f32 size) { m_body->FontSize.SetValue(Optional<f32>{size}); }

        /// The accepted type that means ANY asset: a row whose field is genuinely untyped.
        static constexpr StringView kAnyAsset = u8"*";

        /// The accepted type of an ENTITY reference: the slot takes a hierarchy row, and no asset.
        static constexpr StringView kEntity = u8"@entity";
        /// The item kind a tree row carries when it names an entity (TreeDragData::ItemKind, set
        /// by the hierarchy through DraggableTreeView::OnDecorateDragData).
        static constexpr StringView kEntityItemKind = u8"entity";

        /// The asset-type names this slot accepts (the picker's filter list). Non-empty makes
        /// the slot a drop target: kAnyAsset accepts every asset type, kEntity a hierarchy row.
        /// Empty is not a drop target at all.
        void SetAcceptedTypes(Array<String> types) { m_acceptedTypes = Move(types); }
        [[nodiscard]] Span<const String> AcceptedTypes() const noexcept
        {
            return Span<const String>{m_acceptedTypes.Data(), m_acceptedTypes.Size()};
        }

        /// The warning for a refused drop, naming every accepted type from the row's own list:
        /// "<asset> is a <type> - this <noun> takes <A>, <B> or <C>".
        [[nodiscard]] static String RejectionText(StringView assetName, StringView typeName,
                                                  Span<const String> accepted, StringView noun)
        {
            String wanted;
            for (usize i = 0; i < accepted.Size(); ++i)
            {
                if (i > 0)
                {
                    wanted.Append(i + 1 == accepted.Size() ? StringView(u8" or ")
                                                           : StringView(u8", "));
                }
                wanted.Append(TypeLabel(accepted[i].AsView()));
            }
            const StringView label = TypeLabel(typeName);
            return Format(u8"{} is {} {} - this {} takes {}", assetName, Article(label), label,
                          noun, wanted.IsEmpty() ? StringView(u8"no asset") : wanted.AsView());
        }

        /// "an" before a vowel, "a" otherwise: the rejection text's article.
        [[nodiscard]] static StringView Article(StringView word)
        {
            const utf8char first = word.IsEmpty() ? u8'x' : word[0];
            for (const utf8char vowel : StringView(u8"AEIOUaeiou"))
            {
                if (first == vowel)
                {
                    return u8"an";
                }
            }
            return u8"a";
        }

        /// An accepted or dragged type as a person reads it: the markers by their meaning.
        [[nodiscard]] static StringView TypeLabel(StringView typeName)
        {
            if (typeName == kEntity)
            {
                return u8"entity";
            }
            if (typeName == kAnyAsset)
            {
                return u8"any asset";
            }
            return typeName;
        }

        /// What a drag carries for a slot: an asset (its id, type and name), or a tree row that
        /// names an entity (type kEntity).
        struct DraggedItem
        {
            Guid id;
            String typeName;
            String name;
        };
        /// False for a drag that is neither.
        [[nodiscard]] static bool DescribeDrag(ui::DragData* data, DraggedItem& out)
        {
            if (auto* asset = Cast<AssetDragData>(data))
            {
                out.id = asset->Id;
                out.typeName = asset->AssetTypeName;
                out.name = asset->DisplayName;
                return true;
            }
            if (auto* row = Cast<ui::toolkit::TreeDragData>(data))
            {
                if (row->ItemKind.AsView() != kEntityItemKind || row->ItemId.IsNil())
                {
                    return false;
                }
                out.id = row->ItemId;
                out.typeName = String(kEntity);
                out.name = row->ItemName;
                return true;
            }
            return false;
        }

        /// Whether `typeName`, a dragged asset's type or kEntity, is one of `accepted`
        /// (kAnyAsset takes every asset and no entity). Shared by the slot and the list widget.
        [[nodiscard]] static bool Accepts(Span<const String> accepted, StringView typeName)
        {
            const bool isEntity = typeName == kEntity;
            for (const String& type : accepted)
            {
                if (type.AsView() == kEntity)
                {
                    if (isEntity)
                    {
                        return true;
                    }
                }
                else if (!isEntity && (type.AsView() == kAnyAsset || type.AsView() == typeName))
                {
                    return true;
                }
            }
            return false;
        }

        // === IDropTarget (asset-browser and hierarchy drags) ===
        // Any asset or entity drag is ACCEPTED at hover level so OnDrop can warn on a type
        // mismatch (the manager never calls OnDrop for a None effect); the hover cue
        // distinguishes match (accent ring) from mismatch (error ring).
        [[nodiscard]] ui::IDropTarget* AsDropTarget() override
        {
            return m_acceptedTypes.Size() > 0 ? this : nullptr;
        }
        [[nodiscard]] ui::DragDropEffects CanAcceptDrop(ui::DragData* data, f32, f32) override
        {
            DraggedItem item;
            return DescribeDrag(data, item) ? ui::DragDropEffects::Link
                                            : ui::DragDropEffects::None;
        }
        void OnDragEnter(ui::DragData* data, f32, f32) override
        {
            DraggedItem item;
            m_dropHover = DescribeDrag(data, item);
            m_dropMatches = m_dropHover && TypeAccepted(item.typeName.AsView());
            Invalidate();
        }
        void OnDragOver(ui::DragData*, f32, f32) override {}
        void OnDragLeave(ui::DragData*) override
        {
            m_dropHover = false;
            Invalidate();
        }
        [[nodiscard]] ui::DragDropEffects OnDrop(ui::DragData* data, f32, f32) override
        {
            m_dropHover = false;
            Invalidate();
            DraggedItem item;
            if (!DescribeDrag(data, item))
            {
                return ui::DragDropEffects::None;
            }
            if (!TypeAccepted(item.typeName.AsView()))
            {
                LOG_WARNING(u8"Assets", u8"'{}' is a {} - this slot does not accept it",
                            item.name, TypeLabel(item.typeName.AsView()));
                if (OnRejectedDrop)
                {
                    OnRejectedDrop(item.name.AsView(), item.typeName.AsView());
                }
                return ui::DragDropEffects::None;
            }
            if (OnAssignDropped)
            {
                OnAssignDropped(item.id);
            }
            return ui::DragDropEffects::Link;
        }

        void OnDraw(ui::UIDrawContext& ctx) override
        {
            ui::FlexLayout::OnDraw(ctx);
            if (m_dropHover)
            {
                // Match = accent ring, mismatch = error ring (themed; literals are fallbacks).
                const Color ring =
                    m_dropMatches
                        ? ResolveStyleColor(ui::StyleProperty::AccentColor,
                                            Color{80.0f / 255.0f, 150.0f / 255.0f,
                                                  240.0f / 255.0f, 1.0f})
                        : ResolveStyleColor(ui::StyleProperty::ErrorColor,
                                            Color{210.0f / 255.0f, 60.0f / 255.0f,
                                                  60.0f / 255.0f, 1.0f});
                ctx.VG().StrokeRect(Rectangle{0, 0, Width(), Height()}, ring, 2.0f);
            }
        }

        [[nodiscard]] bool HasValue() const noexcept { return m_hasValue; }
        [[nodiscard]] ui::Button* BodyButton() noexcept { return m_body; }
        [[nodiscard]] ui::ContentButton* EditButton() noexcept { return m_edit; }
        [[nodiscard]] ui::ContentButton* ClearButton() noexcept { return m_clear; }
        [[nodiscard]] ui::ContentButton* PreviewButton() noexcept { return m_preview; }

    private:
        /// ONE chrome for every trailing action: a ContentButton hosting a 14px drawable view
        /// (the footprint the user signed off on; IconButton's tighter padding looked squished
        /// beside it). Adds the button to the row; returns it (and the drawable host).
        [[nodiscard]] ui::ContentButton* MakeActionButton(ui::DrawablePtr icon, StringView tooltip,
                                                          ui::DrawableView** outDrawable = nullptr)
        {
            auto content =
                MakeRef<ui::DrawableView>(MemoryAllocator(), Move(icon), 14.0f, 14.0f);
            content->KeepAspect = true; // a short row shrinks the glyph, never squashes it
            if (outDrawable != nullptr)
            {
                *outDrawable = content.Get();
            }
            auto button = MakeRef<ui::ContentButton>(MemoryAllocator(),
                                                     RefPtr<ui::View>(content.Get()));
            button->TooltipText = String(tooltip);
            AddView(button.Get());
            return button.Get();
        }

        [[nodiscard]] bool TypeAccepted(StringView typeName) const
        {
            return Accepts(AcceptedTypes(), typeName);
        }

        /// Thumbnail wins; the type icon is the fallback layer.
        void ApplyPreview()
        {
            m_previewDrawable->Drawable =
                m_previewThumbnail ? m_previewThumbnail : ui::DrawablePtr(m_previewIcon);
        }

        /// Visibility follows WIRING (unwired affordances take no space); enabled-state
        /// follows the value (Edit/Clear/reveal are inert on an empty slot).
        void SyncAffordances(bool hasValue)
        {
            const bool preview = static_cast<bool>(OnReveal) || m_previewIcon != nullptr ||
                                 static_cast<bool>(m_previewThumbnail);
            m_preview->Visibility = preview ? ui::Visibility::Visible : ui::Visibility::Gone;
            m_preview->IsEnabled = hasValue && static_cast<bool>(OnReveal);
            m_edit->Visibility =
                static_cast<bool>(OnEdit) ? ui::Visibility::Visible : ui::Visibility::Gone;
            m_edit->IsEnabled = hasValue;
            m_clear->Visibility =
                static_cast<bool>(OnClear) ? ui::Visibility::Visible : ui::Visibility::Gone;
            m_clear->IsEnabled = hasValue;
        }

        ui::ContentButton* m_preview = nullptr;    // owned by m_children
        ui::DrawableView* m_previewDrawable = nullptr; // owned by the preview button
        ui::Button* m_body = nullptr;              // owned by m_children
        ui::ContentButton* m_edit = nullptr;       // owned by m_children
        ui::ContentButton* m_clear = nullptr;      // owned by m_children
        ui::SVGDrawable* m_previewIcon = nullptr;  // borrowed (EditorIcons)
        Array<String> m_acceptedTypes;             // drop filter (empty = not a drop target)
        bool m_dropHover = false;                  // an asset drag is over the slot
        bool m_dropMatches = false;                // ...and its type is accepted
        ui::DrawablePtr m_previewThumbnail;        // owned; wins over the icon while set
        bool m_hasValue = false;
    };

    RTTI_DEFINE_OBJECT(AssetPickerSlot, "rtti::editor::editor::app")
}
