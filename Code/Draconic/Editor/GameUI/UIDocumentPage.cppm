// Draconic::EditorGameUI - the `draconic.editor.gameui` module.
//
// UIDocumentPage (game-ui.md P2): text editing + LIVE PREVIEW for UIDocumentAssets.
// The preview renders through the RUNTIME CONTEXT's UISubsystem - the GAME's context,
// fonts, GameTheme, and VG path - into this page's offscreen target (a dedicated
// preview RootView; it can never leak into game targets). What you see IS the game's
// renderer looking at your document; drift is impossible by construction. The text
// pane is the existing multi-line EditText (honest v1: no code-editor control yet);
// edits rebuild the preview after a short debounce, parse failures keep the last good
// preview with inline status, Save writes the asset + nudges the validating recook.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.gameui;

import draconic.core;
import draconic.content;
import draconic.runtime.client;
import draconic.graphics;
import draconic.rhi;
import draconic.vg.renderer;
import draconic.ui;
import draconic.ui.resource;
import draconic.ui.editor;
import draconic.ui.runtime;
import draconic.ui.subsystem;
import draconic.ui.viewport;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace grt = draconic::runtime;
    namespace gui = draconic::ui;
    namespace guirt = draconic::ui::runtime;
    namespace guivp = draconic::ui::viewport;
    namespace gvgr = draconic::vg::renderer;
    namespace rhi = draconic::rhi;

    class UIDocumentEditorPage final : public app::UIEditorPage
    {
    public:
        UIDocumentEditorPage(EditorContext& context, grt::IApplicationHost& host,
                             guirt::UIHost& uiHost, draconic::content::Instance& instance)
            : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
        {
            m_ui = host.Ctx().GetSubsystem<gui::UISubsystem>();
            SetInstanceId(instance.Id());
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<gui::UIDocumentAsset>(object.Get()))
            {
                m_markup = String(asset->markup.AsView());
            }

            auto row = MakeRef<gui::FlexLayout>(DefaultAllocator());
            row->Direction = gui::Orientation::Horizontal;
            row->Spacing = 8.0f;

            // Left: the text pane (v1 = the multi-line EditText).
            m_editor = MakeRef<gui::EditText>(DefaultAllocator());
            m_editor->Multiline.SetValue(true);
            m_editor->SetText(m_markup.AsView());
            UIDocumentEditorPage* self = this;
            m_editor->OnTextChanged.Add([self](gui::EditText* edit) {
                self->m_markup = String(edit->Text());
                self->MarkDirty();
                self->m_previewDelay = 0.35f;   // debounce: rebuild shortly after typing stops
            });
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Height = gui::SizeSpec::Match();
                row->AddView(m_editor.Get(), lp);
            }

            // Right: inline status over the live preview.
            auto right = MakeRef<gui::FlexLayout>(DefaultAllocator());
            right->Direction = gui::Orientation::Vertical;
            right->Spacing = 4.0f;
            m_status = MakeRef<gui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = gui::SizeSpec::Match();
                right->AddView(m_status.Get(), lp);
            }
            // The preview surface: an offscreen target the RUNTIME UI subsystem renders
            // into (the editor UI just displays the texture).
            m_viewport = MakeRef<guivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = gui::SizeSpec::Match();
                lp->Grow = 1.0f;
                right->AddView(m_viewport.Get(), lp);
            }
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Height = gui::SizeSpec::Match();
                row->AddView(right.Get(), lp);
            }
            m_content = row;
            RebuildPreview();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] gui::View* ContentView() override { return m_content.Get(); }

        [[nodiscard]] Status Save() override
        {
            draconic::content::Instance* instance =
                (m_context->Project() != nullptr)
                    ? m_context->Project()->SourceDb().GetInstance(InstanceId()) : nullptr;
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
            gui::UIDocumentAsset asset;
            asset.markup = String(m_markup.AsView());
            const Status written = instance->WriteObject(asset);
            if (written.IsOk())
            {
                ClearDirty();
                // The VALIDATING cook reports errors/warnings; hot reload updates canvases.
                if (m_context->OnCookRequested) { m_context->OnCookRequested(false); }
            }
            return written;
        }

        void OnUpdate(grt::IApplicationHost&, f32 dt) override
        {
            EnsureViewportBound();
            if (m_previewDelay > 0.0f)
            {
                m_previewDelay -= dt;
                if (m_previewDelay <= 0.0f) { RebuildPreview(); }
            }
        }

        void OnAfterSceneRender(grt::IApplicationHost&,
                                draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid) { return; }
            // Always define the target's layout (the editor UI samples it every frame).
            m_viewport->ClearContent(*frame.encoder);
            if (m_ui == nullptr || m_previewRoot.Get() == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }
            frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                             m_viewport->ColorState(),
                                             rhi::ResourceState::RenderTarget);
            m_ui->RenderPreview(*m_previewRoot, *frame.encoder, m_viewport->ColorTargetView(),
                                m_viewport->ColorFormat(), w, h, frame.frameIndex);
            frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                             rhi::ResourceState::RenderTarget,
                                             rhi::ResourceState::ShaderRead);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        void OnClose() override
        {
            if (m_ui != nullptr && m_previewRoot.Get() != nullptr)
            {
                m_ui->DestroyPreview(m_previewRoot.Get());
            }
            m_previewRoot = nullptr;
            m_viewport->Shutdown();
        }

    private:
        // Same lazy dance as ScenePage: the DISPLAY side of the offscreen target needs
        // the page's hosting window + its VG renderer.
        void EnsureViewportBound()
        {
            gui::RootView* root = m_viewport->Root();
            if (root == nullptr) { return; }
            draconic::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
            if (window == nullptr || window == m_hostWindow) { return; }
            gvgr::VGRenderer* renderer = m_uiHost->RendererFor(window);
            if (renderer == nullptr) { return; }
            if (m_hostWindow == nullptr)
            {
                m_viewport->Initialize(m_host->Graphics()->Raw(), renderer,
                                       m_host->Shell()->Input(), window->Window().Id());
            }
            else
            {
                m_viewport->AttachToWindow(renderer, window->Window().Id());
            }
            m_hostWindow = window;
        }

        void RebuildPreview()
        {
            // Validation pass for the inline status (warnings + parse result)...
            gui::MarkupLoader::Initialize();
            Array<String> warnings;
            RefPtr<gui::View> parsed =
                gui::MarkupLoader::LoadFromString(m_markup.AsView(), nullptr, &warnings);
            if (parsed.Get() == nullptr)
            {
                m_status->SetText(u8"Parse FAILED - showing the last good preview.");
                return;
            }
            // ...then the REAL preview instantiates in the runtime context (game fonts,
            // GameTheme, style resolution - the subsystem's CreatePreview).
            if (m_ui == nullptr)
            {
                m_status->SetText(u8"No runtime UI subsystem - preview unavailable.");
                return;
            }
            gui::UIDocument document;
            document.markup = String(m_markup.AsView());
            RefPtr<gui::RootView> fresh = m_ui->CreatePreview(document);
            if (fresh.Get() == nullptr)
            {
                m_status->SetText(u8"Parse FAILED - showing the last good preview.");
                return;
            }
            if (m_previewRoot.Get() != nullptr) { m_ui->DestroyPreview(m_previewRoot.Get()); }
            m_previewRoot = fresh;
            if (warnings.IsEmpty()) { m_status->SetText(u8"OK"); }
            else
            {
                String text(u8"Warnings: ");
                text.Append(warnings[0].AsView());
                if (warnings.Size() > 1) { text.Append(u8" (+more)"); }
                m_status->SetText(text.AsView());
            }
        }

        EditorContext* m_context = nullptr;
        grt::IApplicationHost* m_host = nullptr;
        guirt::UIHost* m_uiHost = nullptr;
        gui::UISubsystem* m_ui = nullptr;   // the RUNTIME context's subsystem
        draconic::graphics::RenderWindow* m_hostWindow = nullptr;
        String m_title;
        String m_markup;
        f32 m_previewDelay = 0.0f;
        RefPtr<gui::View> m_content;
        RefPtr<gui::EditText> m_editor;
        RefPtr<gui::Label> m_status;
        RefPtr<guivp::ViewportView> m_viewport;
        RefPtr<gui::RootView> m_previewRoot;   // lives in the RUNTIME context
    };

    class UIDocumentPageFactory final : public IEditorPageFactory
    {
    public:
        UIDocumentPageFactory(grt::IApplicationHost& host, guirt::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &gui::UIDocumentAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<UIDocumentEditorPage>(context, *m_host, *m_uiHost,
                                                                       instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

    private:
        grt::IApplicationHost* m_host;
        guirt::UIHost* m_uiHost;
    };

    /// The editor executable's entry point for the game-UI plugin.
    inline void RegisterGameUIEditor(EditorContext& context, grt::IApplicationHost& host,
                                     guirt::UIHost& uiHost)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<UIDocumentPageFactory>(host, uiHost), DefaultAllocator()));
    }
}
