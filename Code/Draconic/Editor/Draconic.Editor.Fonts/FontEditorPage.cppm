// Draconic::EditorFonts - the `draconic.editor.fonts` module.
//
// FontEditorPage: the bespoke authoring page for FontAsset (the fonts-triad source asset).
// Left: a live CPU bake preview - the atlas the cook would produce at the preview size
// (coverage expands to RGBA8; MSDF shows the raw field channels), rebaked on every option
// commit - plus source facts (file, family, glyph count, atlas occupancy). Right: the
// authored surface in a PropertyGrid - family, bake mode, the size ramp (comma-separated),
// MSDF size, codepoint range, atlas dimensions. Edits are whole-asset blob-snapshot undo
// commands; Save writes the object back and requests a re-cook so bound products hot-swap.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Log/Log.h"

export module draconic.editor.fonts;

import draconic.core;
import draconic.content;
import draconic.image;
import draconic.fonts;
import draconic.fonts.editor;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace ui = draconic::ui;
    namespace image = draconic::image;
    namespace fonts = draconic::fonts;

    // Authoring page for a FontAsset (bake intent + live atlas preview).
    class FontEditorPage final : public app::UIEditorPage
    {
    public:
        FontEditorPage(EditorContext& context, draconic::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

    private:
        // Bake the preview atlas with the asset's CURRENT options (same machinery as the
        // cook) and refresh the info line. Coverage bakes at the ramp's largest size.
        void RebakePreview();
        void BuildGrid();
        void CommitEdit(StringView mergeKey);

        [[nodiscard]] Array<byte> Snapshot() const;
        void ApplyBlob(const Array<byte>& blob);
        void RefreshRows(); // re-pull row values after undo/redo (never rebuilds the grid)

        class EditFontCommand final : public IEditorCommand
        {
        public:
            EditFontCommand(FontEditorPage& page, StringView mergeKey, Array<byte> before,
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
            [[nodiscard]] StringView TypeId() const override { return u8"edit_font"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditFontCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            FontEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

        EditorContext* m_context = nullptr;
        String m_title;
        RefPtr<fonts::FontAsset> m_asset;
        UniquePtr<image::OwnedImageData> m_preview; // kept alive for the ImageView (borrowed ptr)
        usize m_previewGlyphs = 0;
        f32 m_previewSize = 0.0f;

        RefPtr<ui::View> m_content;
        RefPtr<ui::ImageView> m_image;
        RefPtr<ui::Label> m_info;
        RefPtr<ui::toolkit::PropertyGrid> m_grid;
        // Borrowed rows (grid owns) - re-pulled after undo/redo.
        ui::toolkit::StringEditor* m_familyRow = nullptr;
        ui::toolkit::EnumEditor* m_modeRow = nullptr;
        ui::toolkit::StringEditor* m_sizesRow = nullptr;
        ui::toolkit::FloatEditor* m_dfSizeRow = nullptr;
        ui::toolkit::IntEditor* m_firstRow = nullptr;
        ui::toolkit::IntEditor* m_lastRow = nullptr;
        ui::toolkit::IntEditor* m_atlasWidthRow = nullptr;
        ui::toolkit::IntEditor* m_atlasHeightRow = nullptr;
        Array<byte> m_undoBaseline;
    };

    class FontEditorPageFactory final : public IEditorPageFactory
    {
    public:
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, draconic::content::Instance& instance) override;
    };

    inline void RegisterFontEditor(EditorContext& context)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<FontEditorPageFactory>(), DefaultAllocator()));
    }
}
