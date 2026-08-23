// Editor::GameUI :theme - UIThemeEditorPage method bodies (kept out of the interface unit).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.gameui;

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.runtime.client;
import foundation.graphics;
import foundation.rhi;
import foundation.vg.renderer;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.xml;
import foundation.ui.resource;
import ui.pipeline;
import foundation.ui.runtime;
import engine.ui;
import foundation.ui.viewport;
import editor.core;
import editor.app;

using namespace foundation::core;
namespace rhi = foundation::rhi;
namespace runtime = foundation::runtime;
namespace ui = foundation::ui;
namespace vg = foundation::vg;

namespace editor
{
    String UIThemeEditorPage::ReadLinkedSource(StringView fileName) const
    {
        String sourcesRoot;
        if (m_context->Project() != nullptr)
        {
            sourcesRoot = m_context->Project()->SourcesRoot();
        }
        const String path = PathJoin(sourcesRoot.AsView(), fileName);
        if (Result<Array<byte>> bytes = ReadFile(path.AsView()); bytes.HasValue())
        {
            const Array<byte>& data = bytes.Value();
            return String(StringView(reinterpret_cast<const utf8char*>(data.Data()), data.Size()));
        }
        return String{};
    }

    Status UIThemeEditorPage::Save()
    {
        foundation::content::Instance* instance =
            (m_context->Project() != nullptr)
                ? m_context->Project()->SourceDb().GetInstance(InstanceId())
                : nullptr;
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        RefPtr<ISerializable> object = instance->ReadObject();
        auto* asset = Cast<pipeline::UIThemeAsset>(object.Get());
        if (asset == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        if (!asset->fileName.IsEmpty())
        {
            // LINKED: the stylesheet lives in the Sources/ file (like the document page's markup).
            String sourcesRoot;
            if (m_context->Project() != nullptr)
            {
                sourcesRoot = m_context->Project()->SourcesRoot();
            }
            const String path = PathJoin(sourcesRoot.AsView(), asset->fileName.View());
            const Status fileWritten = WriteFile(path.AsView(),
                                                 Span<const byte>(reinterpret_cast<const byte*>(
                                                                      m_stylesheet.Data()),
                                                                  m_stylesheet.Size()));
            if (!fileWritten.IsOk())
            {
                return fileWritten;
            }
        }
        else
        {
            asset->stylesheet = String(m_stylesheet.AsView()); // LEGACY inline
        }
        // The preview markup is EDITOR-ONLY, persisted inline on the asset (never cooked).
        asset->previewMarkup = String(m_previewMarkup.AsView());
        const Status written = instance->WriteObject(*asset);
        if (written.IsOk())
        {
            ClearDirty();
            if (m_context->OnCookRequested)
            {
                m_context->OnCookRequested(false);
            }
        }
        return written;
    }

    void UIThemeEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        EnsureViewportBound();
        if (m_previewDelay > 0.0f)
        {
            m_previewDelay -= dt;
            if (m_previewDelay <= 0.0f)
            {
                RebuildPreview();
            }
        }
    }

    void UIThemeEditorPage::OnAfterSceneRender(runtime::IApplicationHost&,
                                               foundation::graphics::FrameContext& frame)
    {
        if (!m_viewport->IsReady() || !frame.valid)
        {
            return;
        }
        m_viewport->ClearContent(*frame.encoder);
        if (m_ui == nullptr || m_previewRoot.Get() == nullptr)
        {
            return;
        }
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (w == 0 || h == 0)
        {
            return;
        }
        frame.encoder->TransitionTexture(m_viewport->ColorTexture(), m_viewport->ColorState(),
                                         rhi::ResourceState::RenderTarget);
        m_ui->RenderPreview(*m_previewRoot, *frame.encoder, m_viewport->ColorTargetView(),
                            m_viewport->ColorFormat(), w, h, frame.frameIndex);
        frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                         rhi::ResourceState::RenderTarget,
                                         rhi::ResourceState::ShaderRead);
        m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
    }

    void UIThemeEditorPage::OnClose()
    {
        if (m_ui != nullptr && m_previewRoot.Get() != nullptr)
        {
            m_ui->DestroyPreview(m_previewRoot.Get());
        }
        m_previewRoot = nullptr;
        m_viewport->Shutdown();
    }

    void UIThemeEditorPage::EnsureViewportBound()
    {
        ui::RootView* root = m_viewport->Root();
        if (root == nullptr)
        {
            return;
        }
        foundation::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
        if (window == nullptr || window == m_hostWindow)
        {
            return;
        }
        vg::renderer::VGRenderer* renderer = m_uiHost->RendererFor(window);
        if (renderer == nullptr)
        {
            return;
        }
        if (m_hostWindow == nullptr)
        {
            m_viewport->Initialize(m_host->Graphics()->Raw(), renderer, m_host->Shell()->Input(),
                                   window->Window().Id());
        }
        else
        {
            m_viewport->AttachToWindow(renderer, window->Window().Id());
        }
        m_hostWindow = window;
    }

    void UIThemeEditorPage::RebuildPreview()
    {
        ui::MarkupLoader::Initialize();
        // Diagnostics for the PREVIEW-MARKUP editor (the SSS editor has no line info from the loader).
        {
            foundation::xml::XmlDocument probe;
            const foundation::xml::XmlResult result = probe.Parse(m_previewMarkup.AsView());
            Array<ui::toolkit::CodeDiagnostic> diagnostics;
            if (foundation::xml::IsError(result))
            {
                ui::toolkit::CodeDiagnostic diagnostic;
                diagnostic.isError = true;
                diagnostic.line = probe.ErrorLine() - 1;
                diagnostic.message = String(foundation::xml::Describe(result));
                diagnostics.PushBack(Move(diagnostic));
            }
            m_previewEditor->Document().SetDiagnostics(Move(diagnostics));
            m_previewEditor->Invalidate();
        }
        if (m_ui == nullptr)
        {
            m_status->SetText(u8"No runtime UI subsystem - preview unavailable.");
            return;
        }
        // Instantiate the preview MARKUP in the runtime context (game fonts + VG path).
        ui::UIDocument document;
        document.markup = String(m_previewMarkup.AsView());
        RefPtr<ui::RootView> fresh = m_ui->CreatePreview(document);
        if (fresh.Get() == nullptr)
        {
            m_status->SetText(u8"Preview markup parse FAILED - showing the last good preview.");
            return;
        }
        // Apply the EDITED stylesheet PER-ELEMENT (subtree-scoped) - never the shared game context.
        ui::StyleSheetLoader loader;
        loader.SetPalette(ui::ThemePalette::Dark());
        RefPtr<ui::StyleSheet> sheet = loader.Load(m_stylesheet.AsView());
        String status;
        if (sheet.Get() != nullptr)
        {
            fresh->SetLocalStyleSheet(sheet);
            fresh->Invalidate(); // restyle the subtree at the next layout (render)
            status = String(u8"OK");
        }
        else
        {
            status = String(u8"Stylesheet parse FAILED - preview uses the default theme.");
        }
        if (m_previewRoot.Get() != nullptr)
        {
            m_ui->DestroyPreview(m_previewRoot.Get());
        }
        m_previewRoot = fresh;
        m_status->SetText(status.AsView());
    }

    void UIThemeEditorPage::PickPreviewDocument()
    {
        if (m_context->Project() == nullptr || m_content.Get() == nullptr)
        {
            return;
        }
        Array<String> typeNames;
        typeNames.PushBack(String(u8"UIDocumentAsset"));
        auto dialog = MakeRef<app::AssetPickerDialog>(DefaultAllocator(), *m_context, Move(typeNames));
        UIThemeEditorPage* self = this;
        dialog->OnPicked = [self](const Guid& picked)
        {
            if (picked.IsNil() || self->m_context->Project() == nullptr)
            {
                return;
            }
            foundation::content::Instance* inst =
                self->m_context->Project()->SourceDb().GetInstance(picked);
            if (inst == nullptr)
            {
                return;
            }
            RefPtr<ISerializable> obj = inst->ReadObject();
            auto* doc = Cast<pipeline::UIDocumentAsset>(obj.Get());
            if (doc == nullptr)
            {
                return;
            }
            const String markup = doc->fileName.IsEmpty()
                                      ? String(doc->markup.AsView())
                                      : self->ReadLinkedSource(doc->fileName.View());
            self->m_previewMarkup = markup;
            self->m_previewEditor->SetText(markup.AsView()); // copy-once; now the user's to tweak
            self->MarkDirty();                               // persisted on the theme asset
            self->m_previewDelay = 0.05f;                    // rebuild promptly
        };
        dialog->Show(m_content->Context);
    }

    const TypeInfo* UIThemePageFactory::PrimaryType() const
    {
        return &pipeline::UIThemeAsset::StaticType();
    }

    UniquePtr<EditorPage> UIThemePageFactory::CreatePage(EditorContext& context,
                                                         foundation::content::Instance& instance)
    {
        auto* page =
            DefaultAllocator().New<UIThemeEditorPage>(context, *m_host, *m_uiHost, instance);
        return UniquePtr<EditorPage>(page, DefaultAllocator());
    }
}
