// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::GameUI :theme partition - the bespoke editor for UIThemeAssets (.sss).
//
// Mirrors UIDocumentEditorPage, but the ASSET is the stylesheet and the preview needs MARKUP to style.
// So the page carries TWO editors: the SSS (the theme, saved to the linked Sources/ file) is the main
// pane; a PREVIEW-MARKUP editor holds scaffolding SML the theme is previewed against (persisted on the
// asset's editor-only previewMarkup, NOT cooked). A "Preview Document" picker reads a chosen
// UIDocumentAsset's markup into the preview-markup editor; from there it can be tweaked inline.
//
// The preview renders through the RUNTIME context's UISubsystem (game fonts + VG path) into an offscreen
// target, exactly like the document page - but the edited stylesheet is applied PER-ELEMENT via
// RootView::SetLocalStyleSheet (subtree-scoped), NEVER on the shared game context (that would restyle
// the whole game UI). The local sheet cascades over the context's default theme (partial-override
// preview); a dedicated preview context would be the fallback if pure isolation is ever needed.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.gameui:theme;

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.runtime.client;
import foundation.graphics;
import foundation.rhi;
import foundation.vg.renderer;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.resource;
import ui.pipeline;
import foundation.ui.runtime;
import engine.ui;
import foundation.ui.viewport;
import editor.core;
import editor.app;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace vg = foundation::vg;
    namespace rhi = foundation::rhi;

    class UIThemeEditorPage final : public app::UIEditorPage
    {
    public:
        UIThemeEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                          ui::runtime::UIHost& uiHost, foundation::content::Instance& instance)
            : app::UIEditorPage(context.Allocator()),
              m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
        {
            m_ui = host.Ctx().GetSubsystem<engine::ui::UISubsystem>();
            SetInstanceId(instance.Id());
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<pipeline::UIThemeAsset>(object.Get()))
            {
                // The stylesheet lives in the LINKED Sources/ file (fileName), like scripts / the
                // document page. previewMarkup is editor-only, inline.
                if (!asset->fileName.IsEmpty())
                {
                    m_stylesheet = ReadLinkedSource(asset->fileName.View());
                }
                else
                {
                    LOG_ERROR(u8"Editor", u8"UI theme has no linked source file - the page "
                                          u8"opens empty (re-import the .sss)");
                }
                m_previewMarkup = String(asset->previewMarkup.AsView());
            }
            if (m_previewMarkup.IsEmpty())
            {
                m_previewMarkup = String(kStockPreviewMarkup); // preview against SOMETHING out of the box
            }

            auto row = MakeRef<ui::FlexLayout>(Allocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8.0f;

            // Left: the SSS editor (the theme itself - the saved asset). CLikeLexer is the closest fit
            // for the CSS-like SSS grammar (braces / strings / numbers / comments) until an SSS lexer.
            m_editor = MakeRef<ui::toolkit::CodeEditView>(Allocator());
            m_editor->AllowBreakpoints = false;
            // SSS is CSS-like; the toolkit ships no SSS lexer yet, so v1 is plain text (gutter,
            // monospace, native undo) - syntax highlighting is a later refinement.
            m_editor->SetText(m_stylesheet.AsView());
            UIThemeEditorPage* self = this;
            m_editor->OnTextChanged.Add(
                [self]()
                {
                    self->m_stylesheet = self->m_editor->Text();
                    self->MarkDirty();
                    self->m_previewDelay = 0.35f;
                });
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Grow = 1.0f;
                lp->Height = ui::SizeSpec::Match();
                row->AddView(m_editor.Get(), lp);
            }

            // Right column: [pick button + status] / preview-markup editor / live preview.
            auto right = MakeRef<ui::FlexLayout>(Allocator());
            right->Direction = ui::Orientation::Vertical;
            right->Spacing = 4.0f;

            auto bar = MakeRef<ui::FlexLayout>(Allocator());
            bar->Direction = ui::Orientation::Horizontal;
            bar->Spacing = 8.0f;
            m_pickButton = MakeRef<ui::Button>(Allocator(), StringView(u8"Preview Document..."));
            m_pickButton->OnClick.Add([self](ui::ButtonBase*) { self->PickPreviewDocument(); });
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                bar->AddView(m_pickButton.Get(), lp);
            }
            m_status = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Grow = 1.0f;
                bar->AddView(m_status.Get(), lp);
            }
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Width = ui::SizeSpec::Match();
                right->AddView(bar.Get(), lp);
            }

            // Preview markup (SML) - scaffolding only; XML lexer + tag completion like the document page.
            m_previewEditor = MakeRef<ui::toolkit::CodeEditView>(Allocator());
            m_previewEditor->AllowBreakpoints = false;
            m_previewEditor->SetLexer(UniquePtr<ui::toolkit::ICodeLexer>(
                Allocator().New<ui::toolkit::XmlLexer>(), Allocator()));
            m_previewEditor->CompletionTriggerCharacters = String(u8"<");
            m_previewEditor->AddCompletionProvider(&m_markupProvider);
            m_previewEditor->SetText(m_previewMarkup.AsView());
            m_previewEditor->OnTextChanged.Add(
                [self]()
                {
                    self->m_previewMarkup = self->m_previewEditor->Text();
                    self->MarkDirty(); // the preview markup is persisted on the asset
                    self->m_previewDelay = 0.35f;
                });
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                right->AddView(m_previewEditor.Get(), lp);
            }

            // The live preview surface (offscreen; the runtime UI subsystem renders into it).
            m_viewport = MakeRef<ui::viewport::ViewportView>(Allocator());
            m_viewport->ClearColor = rhi::ClearColor{0.08f, 0.09f, 0.11f, 1.0f};
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Grow = 2.0f; // the render gets the lion's share of the right column
                right->AddView(m_viewport.Get(), lp);
            }
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
                lp->Grow = 1.0f;
                lp->Height = ui::SizeSpec::Match();
                row->AddView(right.Get(), lp);
            }
            m_content = row;
            RebuildPreview();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }

        [[nodiscard]] Status Save() override;
        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;
        void OnAfterSceneRender(runtime::IApplicationHost&,
                                foundation::graphics::FrameContext& frame) override;
        void OnClose() override;

    private:
        void EnsureViewportBound();
        void RebuildPreview();
        void PickPreviewDocument();
        [[nodiscard]] String ReadLinkedSource(StringView fileName) const;

        // A small stock document so a brand-new theme (no preview markup yet) previews against something.
        static constexpr StringView kStockPreviewMarkup =
            u8"<Panel padding=\"16\">\n"
            u8"  <Label text=\"Heading\" class=\"heading\"/>\n"
            u8"  <Label text=\"Body text sample.\"/>\n"
            u8"  <Button text=\"Primary\" class=\"primary\"/>\n"
            u8"  <Button text=\"Default\"/>\n"
            u8"</Panel>\n";

        EditorContext* m_context = nullptr;
        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        engine::ui::UISubsystem* m_ui = nullptr;
        foundation::graphics::RenderWindow* m_hostWindow = nullptr;
        String m_title;
        String m_stylesheet;    // the theme (linked .sss source); the SAVED asset content
        String m_previewMarkup; // editor-only scaffolding; persisted on the asset, never cooked
        f32 m_previewDelay = 0.0f;
        ui::toolkit::MarkupCompletionProvider m_markupProvider; // borrowed by the preview editor
        RefPtr<ui::View> m_content;
        RefPtr<ui::toolkit::CodeEditView> m_editor;        // SSS
        RefPtr<ui::toolkit::CodeEditView> m_previewEditor; // preview SML
        RefPtr<ui::Button> m_pickButton;
        RefPtr<ui::Label> m_status;
        RefPtr<ui::viewport::ViewportView> m_viewport;
        RefPtr<ui::RootView> m_previewRoot; // lives in the RUNTIME context
    };

    class UIThemePageFactory final : public IEditorPageFactory
    {
    public:
        UIThemePageFactory(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost)
        {
        }
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
    };
}
