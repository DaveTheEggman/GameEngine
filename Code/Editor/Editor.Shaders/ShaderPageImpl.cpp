// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Shaders - ShaderEditorPage (declared in :shader_page).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.shaders;

import foundation.core;
import foundation.content;
import foundation.graphics;
import foundation.runtime;
import foundation.runtime.client;
import foundation.scene;
import foundation.geometry;
import foundation.shaders;
import foundation.shaders.system;
import foundation.shaders.resource;
import foundation.materials;
import foundation.materials.resource;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import engine.render;
import shaders.pipeline;
import materials.pipeline;
import editor.core;
import editor.app;
import editor.preview;

using namespace foundation::core;

namespace editor
{
    namespace scene = foundation::scene;
    namespace shaders = foundation::shaders;
    namespace materials = foundation::materials;

    namespace
    {
        // How long after the last key the text is checked: long enough not to compile on every
        // keystroke, short enough to read as live.
        constexpr f32 kCheckDelay = 0.6f;
    }

    ShaderEditorPage::ShaderEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                       ui::runtime::UIHost& uiHost,
                                       foundation::content::Instance& instance,
                                       StringView shaderLibrary)
        : app::UIEditorPage(context.Allocator()), m_context(&context), m_title(instance.Name()),
          m_shaderLibrary(shaderLibrary)
    {
        SetInstanceId(instance.Id());
        if (auto* render = host.Ctx().GetSubsystem<engine::render::RenderSubsystem>())
        {
            m_shaders = render->Shaders();
        }
        // A name of the page's own: the preview never replaces the cooked shader a scene draws.
        m_previewName = Format(u8"editor.preview.{}", instance.Id());

        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<pipeline::ShaderAsset>(Cast<pipeline::ShaderAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"shader '{}' failed to read - the page opens empty", m_title);
            m_asset = MakeRef<pipeline::ShaderAsset>(Allocator());
        }
        m_fileName = String(m_asset->fileName.View());
        if (m_context->Project() != nullptr && !m_fileName.IsEmpty())
        {
            m_sourcePath = PathJoin(m_context->Project()->SourcesRoot().AsView(), m_fileName.AsView());
            if (Result<Array<byte>> bytes = ReadFile(m_sourcePath.AsView()); bytes.HasValue())
            {
                m_text = String(StringView(reinterpret_cast<const utf8char*>(bytes.Value().Data()),
                                           bytes.Value().Size()));
            }
            else
            {
                LOG_ERROR(u8"Editor", u8"shader '{}': cannot read {} - the page opens empty",
                          m_title, m_sourcePath);
            }
        }
        else
        {
            LOG_ERROR(u8"Editor", u8"shader '{}' has no linked .hlsl - the page opens empty",
                      m_title);
        }

        ShaderEditorPage* self = this;

        // Left: the code, its status line and its error list.
        auto column = MakeRef<ui::FlexLayout>(Allocator());
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 4.0f;
        m_editor = MakeRef<ui::toolkit::CodeEditView>(Allocator());
        m_editor->AllowBreakpoints = false; // a shader has no debugger
        m_editor->SetLexer(ui::toolkit::CodeLexerRegistry::Get().Create(u8"hlsl"));
        m_editor->SetText(m_text.AsView());
        m_editor->OnTextChanged.Add(
            [self]()
            {
                self->m_text = self->m_editor->Text();
                self->MarkDirty();
                self->m_checkDelay = kCheckDelay;
            });
        {
            ui::LayoutStyle lp;
            lp.FlexGrow = 1.0f;
            lp.Width = ui::SizeSpec::Match();
            column->AddView(m_editor.Get(), lp);
        }
        m_status = MakeRef<ui::Label>(Allocator(), StringView(u8""));
        m_status->FontSize.SetValue(12.0f);
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Margin = ui::Thickness{6, 2}; // off the pane edge
            column->AddView(m_status.Get(), lp);
        }
        // A fixed height, as the script page's: a share of the column would grow with the panel
        // and take the code's room.
        m_errorView = MakeRef<ui::EditText>(Allocator());
        m_errorView->Multiline.SetValue(true);
        m_errorView->IsReadOnly.SetValue(true);
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(96));
            column->AddView(m_errorView.Get(), lp);
        }

        // Right: the preview over the asset's fields.
        m_preview = MakeUnique<PreviewViewport>(Allocator(), host, uiHost, u8"shader.preview");
        m_preview->Camera().position = Float3{0.0f, 0.9f, 2.6f};
        m_preview->Camera().LookAt(Float3{0.0f, 0.0f, 0.0f});
        BuildPreviewScene();

        m_grid = MakeRef<ui::toolkit::PropertyGrid>(Allocator());
        const auto addField = [&](StringView label, String pipeline::ShaderAsset::* field)
        {
            auto editor = MakeRef<ui::toolkit::StringEditor>(
                Allocator(), label, (m_asset.Get()->*field).AsView(),
                [self, field](StringView value)
                {
                    if ((self->m_asset.Get()->*field).AsView() == value)
                    {
                        return;
                    }
                    self->m_asset.Get()->*field = String(value);
                    self->MarkDirty();
                    self->m_checkDelay = kCheckDelay; // an entry point changes what compiles
                },
                StringView(u8"Shader"));
            m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(editor.Get()));
        };
        addField(u8"Name", &pipeline::ShaderAsset::name);
        addField(u8"Vertex Entry Point", &pipeline::ShaderAsset::vertexEntryPoint);
        addField(u8"Fragment Entry Point", &pipeline::ShaderAsset::fragmentEntryPoint);

        auto right = MakeRef<ui::FlexLayout>(Allocator());
        right->Direction = ui::Orientation::Vertical;
        right->Spacing = 4.0f;
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.FlexGrow = 1.0f;
            right->AddView(m_preview->View(), lp);
        }
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(120));
            right->AddView(m_grid.Get(), lp);
        }

        auto split = MakeRef<ui::toolkit::SplitView>(Allocator());
        split->SetSplitRatio(0.6f);
        split->SetPanes(column.Get(), right.Get());
        m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this, m_context->Actions(),
                                              app::PageToolbar::Standard::Save);
        m_content = app::PageToolbar::Frame(Allocator(), *m_toolbar, *split);

        Check(); // the page opens with the shader's state, and its preview
    }

    ShaderEditorPage::~ShaderEditorPage()
    {
        RemovePreviewShader();
        if (m_compiler != nullptr)
        {
            m_compiler->Destroy();
        }
    }

    UniquePtr<EditorPage> ShaderEditorPageFactory::CreatePage(EditorContext& context,
                                                              foundation::content::Instance& instance)
    {
        auto* page = editor::EditorRootAllocator().New<ShaderEditorPage>(
            context, *m_host, *m_uiHost, instance, m_shaderLibrary.AsView());
        return UniquePtr<EditorPage>(page, editor::EditorRootAllocator());
    }

    Status ShaderEditorPage::Save()
    {
        if (m_context->Project() == nullptr || m_sourcePath.IsEmpty())
        {
            return Status{ErrorCode::InvalidArgument};
        }
        const Status written = WriteFile(
            m_sourcePath.AsView(),
            Span<const byte>(reinterpret_cast<const byte*>(m_text.Data()), m_text.Size()));
        if (!written.IsOk())
        {
            m_context->Notify(NoticeKind::Error, u8"Shader not saved - its file cannot be written");
            return written;
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        // The asset carries the name and entry points; writing it also marks it for the cook.
        const Status assetWritten = instance->WriteObject(*m_asset);
        if (!assetWritten.IsOk())
        {
            return assetWritten;
        }
        ClearDirty();
        // The cook compiles every variant; a failure keeps the last good shader in the scenes.
        m_context->RequestCook(false);
        Check();
        m_context->Notify(m_errorView->Text().IsEmpty() ? NoticeKind::Success : NoticeKind::Warning,
                          m_errorView->Text().IsEmpty()
                              ? StringView(u8"Shader saved - recooking every variant")
                              : StringView(u8"Shader saved with errors - the last good is kept"));
        return Status{};
    }

    void ShaderEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        if (m_toolbar.Get() != nullptr)
        {
            m_toolbar->Refresh();
        }
        if (m_preview)
        {
            m_preview->Update(dt);
        }
        if (m_checkDelay > 0.0f)
        {
            m_checkDelay -= dt;
            if (m_checkDelay <= 0.0f)
            {
                Check();
            }
        }
    }

    void ShaderEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                          foundation::graphics::FrameContext& frame)
    {
        if (m_preview)
        {
            m_preview->RenderFrame(frame);
        }
    }

    void ShaderEditorPage::OnClose()
    {
        if (m_preview)
        {
            m_preview->Shutdown();
        }
        RemovePreviewShader();
    }

    void ShaderEditorPage::BuildPreviewScene()
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (scenePtr == nullptr)
        {
            return;
        }
        m_sphere = scenePtr->CreateEntity(u8"PreviewSphere");
        m_previewMesh = foundation::geometry::Primitives::Sphere(Allocator(), 1.0f, 48, 24);
        if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
        {
            engine::render::MeshComponent& mc = meshes->Add(m_sphere);
            mc.mesh = m_previewMesh.Get(); // direct override (runtime-built, not an asset)
        }
        const scene::EntityHandle sun = scenePtr->CreateEntity(u8"Sun");
        Transform t;
        t.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.35f) *
                     Quaternion::FromAxisAngle(Float3{1, 0, 0}, -1.05f);
        scenePtr->SetLocalTransform(sun, t);
        if (auto* lights = scenePtr->GetSystem<engine::render::LightComponentManager>())
        {
            engine::render::LightComponent& light = lights->Add(sun);
            light.castsShadows = false; // a lone sphere has nothing to shadow
        }
    }

    void ShaderEditorPage::Check()
    {
        if (m_compiler == nullptr &&
            (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() ||
             m_compiler == nullptr))
        {
            m_compiler = nullptr;
        }
        pipeline::ShaderCheckOptions options;
        options.vertexEntryPoint = m_asset->vertexEntryPoint.AsView();
        options.fragmentEntryPoint = m_asset->fragmentEntryPoint.AsView();
        options.shaderLibrary = m_shaderLibrary.AsView();
        options.depth = pipeline::ShaderCheckDepth::BaseVariants;
        options.compiler = m_compiler;
        const pipeline::ShaderCheck check =
            pipeline::CheckShaderSource(m_text.AsView(), m_fileName.AsView(), options);

        // This file's errors on their lines; an included file's only in the list.
        Array<ui::toolkit::CodeDiagnostic> diagnostics;
        String detail;
        for (const pipeline::ShaderDiagnostic& d : check.errors)
        {
            if (d.file.AsView() == m_fileName.AsView() && d.line > 0)
            {
                ui::toolkit::CodeDiagnostic diagnostic;
                diagnostic.isError = true;
                diagnostic.line = static_cast<i32>(d.line) - 1;
                diagnostic.message = String(d.message.AsView());
                diagnostics.PushBack(Move(diagnostic));
            }
            if (!detail.IsEmpty())
            {
                detail.PushBack(utf8char('\n'));
            }
            detail.Append(pipeline::FormatShaderDiagnostic(d).AsView());
        }
        m_editor->Document().SetDiagnostics(Move(diagnostics));
        m_editor->Invalidate();
        m_errorView->SetText(detail.AsView());
        if (check.IsOk())
        {
            const StringView kind = check.surface == pipeline::SurfaceModel::Lit     ? StringView(u8"Lit surface shader")
                                    : check.surface == pipeline::SurfaceModel::Unlit ? StringView(u8"Unlit surface shader")
                                                                                     : StringView(u8"Low-level shader");
            m_status->SetText(Format(u8"{}: compiles ({} properties). Save checks every variant.",
                                     kind, check.block.properties.Size())
                                  .AsView());
            UpdatePreview(check);
            return;
        }
        // The preview keeps the last text that compiled.
        m_status->SetText(check.errors.Size() == 1
                              ? StringView(u8"1 error")
                              : Format(u8"{} errors", check.errors.Size()).AsView());
    }

    void ShaderEditorPage::UpdatePreview(const pipeline::ShaderCheck& check)
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (m_shaders == nullptr || scenePtr == nullptr || !m_sphere.IsAssigned())
        {
            return;
        }
        // The main program as the cook would ship it (a surface shader's forward program),
        // under the page's own name, with its entries and lattices.
        const pipeline::ShaderCheckProgram& main = check.programs[0];
        const StringView name = m_previewName.AsView();
        m_shaders->RegisterSource(name, shaders::ShaderStage::Vertex, main.text.AsView(),
                                  main.vertexEntryPoint.AsView(), main.vertexMask);
        m_shaders->RegisterSource(name, shaders::ShaderStage::Fragment, main.text.AsView(),
                                  main.fragmentEntryPoint.AsView(), main.fragmentMask);
        m_shaders->InvalidateShader(name);

        // A material laid out by the block as a cooked material on this shader is, at its
        // defaults.
        shaders::ShaderSource shader;
        shader.name = m_previewName;
        shader.SetProperties(check.block);
        materials::MaterialSource authored;
        authored.name = String(u8"preview");
        materials::MaterialSource laidOut;
        pipeline::LayOutMaterialByShader(authored, shader, laidOut);
        m_previewMaterial = materials::MaterialFromSource(Allocator(), laidOut, name);
        if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
        {
            if (engine::render::MeshComponent* mc = meshes->Get(m_sphere))
            {
                mc->SetMaterial(RefPtr<materials::Material>(m_previewMaterial.Get()));
            }
        }
    }

    void ShaderEditorPage::RemovePreviewShader()
    {
        if (m_shaders != nullptr && !m_previewName.IsEmpty())
        {
            m_shaders->RemoveSource(m_previewName.AsView());
            m_shaders->InvalidateShader(m_previewName.AsView());
        }
    }
}
