// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Heightfield - the `editor.heightfield` module.
//
// HeightfieldEditorPage: the inspect + author surface over a HeightfieldAsset. A heightfield IS an
// image of heights, so the preview is a 2D grayscale height image (permanent, by asset identity -
// the texture/image page precedent, NOT a placeholder awaiting 3D; the 3D preview belongs to the
// terrain asset page). The authored fields (grid size, world footprint, Y range) edit on
// the right; sculpt brushes live in the scene viewport, not here. Save writes the object back and
// requests a re-cook so bound heightfield products (colliders, later terrain) hot-swap.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.heightfield;

export import :thumbnail_generator;

import foundation.core;
import foundation.content;
import foundation.image;
import foundation.image.io;
import heightfield.pipeline;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;

export namespace editor
{
    namespace ui = foundation::ui;

    // Inspect + author page for a HeightfieldAsset (2D grayscale preview + the authored fields).
    class HeightfieldEditorPage final : public app::UIEditorPage
    {
    public:
        HeightfieldEditorPage(EditorContext& context, foundation::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

    private:
        // Decode the source heightmap (16-bit) into a grayscale RGBA8 buffer kept alive for the view.
        void LoadPreview();
        void BuildGrid();
        void RefreshInfo();

        [[nodiscard]] Array<byte> Snapshot() const;
        void ApplyBlob(const Array<byte>& blob);

        class EditCommand final : public IEditorCommand
        {
        public:
            EditCommand(HeightfieldEditorPage& page, StringView mergeKey, Array<byte> before,
                        Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->ApplyBlob(m_after);
                return true;
            }
            void Undo() override { m_page->ApplyBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_heightfield"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            HeightfieldEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

        // Push an undo command for the current asset state under a merge key, then mark dirty.
        void CommitEdit(StringView mergeKey);

        EditorContext* m_context = nullptr;
        String m_title;
        RefPtr<pipeline::HeightfieldAsset> m_asset;
        UniquePtr<foundation::image::OwnedImageData> m_preview; // kept alive for the ImageView
        u32 m_sourceWidth = 0;
        u32 m_sourceHeight = 0;
        u16 m_minSample = 0;
        u16 m_maxSample = 0;

        RefPtr<ui::View> m_content;
        RefPtr<ui::ImageView> m_image;
        RefPtr<ui::Label> m_info;
        RefPtr<ui::toolkit::PropertyGrid> m_grid;
        Array<byte> m_undoBaseline;
    };

    class HeightfieldEditorPageFactory final : public IEditorPageFactory
    {
    public:
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;
    };

    inline void RegisterHeightfieldEditor(EditorContext& context)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            foundation::core::DefaultAllocator().New<HeightfieldEditorPageFactory>(), foundation::core::DefaultAllocator()));
        if (context.Thumbnails() != nullptr)
        {
            RegisterHeightfieldThumbnailGenerator(*context.Thumbnails());
        }
    }
}
