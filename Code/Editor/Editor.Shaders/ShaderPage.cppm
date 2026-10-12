// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Shaders - the `:shader_page` partition: a custom shader's page.
//
// The shader's .hlsl (its asset's linked Sources/ file) in a code editor, checked as you type
// (CheckShaderSource on each stage's base variant, a short pause after the last key) with the
// errors as line markers, a list and a status line; beside it a sphere drawn with the text being
// edited, its material laid out by the properties block exactly as a cooked material is, and the
// asset's name and entry points. Save writes the file and the asset and recooks, which compiles
// every variant; the preview draws under a name of its own, so a scene keeps the last cooked
// shader until then.

module;
#include "Core/Prelude.h"

export module editor.shaders:shader_page;

import foundation.core;
import foundation.content;
import foundation.graphics;
import foundation.runtime;
import foundation.runtime.client;
import foundation.scene;
import foundation.geometry;
import foundation.shaders;
import foundation.shaders.system;
import foundation.materials;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import engine.render;
import shaders.pipeline;
import editor.core;
import editor.app;
import editor.preview;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;

    class ShaderEditorPage final : public app::UIEditorPage
    {
    public:
        // `shaderLibrary`: the engine's Shaders/ folder, the include path a check compiles with.
        ShaderEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                         ui::runtime::UIHost& uiHost, foundation::content::Instance& instance,
                         StringView shaderLibrary);
        ~ShaderEditorPage();

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;
        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;
        void OnRenderWindow(runtime::IApplicationHost&,
                            foundation::graphics::FrameContext& frame) override;
        void OnClose() override;

    private:
        void BuildPreviewScene();
        // Checks the text, puts the errors on the page, and on success redraws the preview.
        void Check();
        void UpdatePreview(const pipeline::ShaderCheck& check);
        void RemovePreviewShader();

        EditorContext* m_context = nullptr;
        foundation::shaders::ShaderSystem* m_shaders = nullptr; // the renderer's; borrowed
        String m_title;
        String m_shaderLibrary;
        String m_sourcePath; // absolute path of the linked .hlsl
        String m_fileName;   // its name as errors carry it
        String m_previewName; // the preview's shader name, private to this page
        RefPtr<pipeline::ShaderAsset> m_asset; // the edited asset (Save writes it back)
        String m_text;
        f32 m_checkDelay = 0.0f;
        foundation::shaders::Compiler* m_compiler = nullptr; // reused by every check

        UniquePtr<PreviewViewport> m_preview;
        foundation::scene::EntityHandle m_sphere{};
        RefPtr<foundation::geometry::StaticMesh> m_previewMesh;
        RefPtr<foundation::materials::Material> m_previewMaterial;

        RefPtr<ui::View> m_content;
        RefPtr<app::PageToolbar> m_toolbar;
        RefPtr<ui::toolkit::CodeEditView> m_editor;
        RefPtr<ui::Label> m_status;
        RefPtr<ui::EditText> m_errorView;
        RefPtr<ui::toolkit::PropertyGrid> m_grid;
    };

    class ShaderEditorPageFactory final : public IEditorPageFactory
    {
    public:
        ShaderEditorPageFactory(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost,
                                StringView shaderLibrary)
            : m_host(&host), m_uiHost(&uiHost), m_shaderLibrary(shaderLibrary)
        {
        }
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &pipeline::ShaderAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
        String m_shaderLibrary;
    };

    /// The editor executable's entry point for the shader page. `shaderLibrary`: the engine's
    /// Shaders/ folder under the data root.
    inline void RegisterShaderEditor(EditorContext& context, runtime::IApplicationHost& host,
                                     ui::runtime::UIHost& uiHost, StringView shaderLibrary)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            editor::EditorRootAllocator().New<ShaderEditorPageFactory>(host, uiHost, shaderLibrary),
            editor::EditorRootAllocator()));
    }
}
