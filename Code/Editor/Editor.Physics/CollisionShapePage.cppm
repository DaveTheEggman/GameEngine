// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Physics - the `editor.physics` module (tooling).
//
// CollisionShapeEditorPage: the bespoke authoring page for a CollisionShapeAsset. It replaces the
// generic asset form (a raw guid row) with a TYPED mesh picker (no guid string), a cook-kind toggle
// (convex hull / triangle mesh), a "Cook now" action, and a one-line status naming the source mesh.
// Save writes the asset back and requests a re-cook so a shape=Cooked body picks up the new collider.
// Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.physics;

export import :collision_thumbnail;

import foundation.core;
import foundation.content;
import foundation.runtime;
import foundation.runtime.client;
import foundation.resource;
import physics.pipeline;             // CollisionShapeAsset + CollisionCookKind
import foundation.physics.resource;  // CollisionShape product (cooked outline)
import foundation.graphics;          // FrameContext
import foundation.ui;
import foundation.ui.runtime;
import foundation.ui.toolkit;        // SplitView
import editor.core;
import editor.app;
import editor.preview;               // PreviewViewport (+ EditorCamera)

using namespace foundation::core;

export namespace editor
{
    namespace ui = foundation::ui;
    namespace runtime = foundation::runtime;
    namespace resource = foundation::resource;
    namespace physics = foundation::physics;

    // Authoring page for a CollisionShapeAsset (pick a mesh, choose the cook, cook it).
    class CollisionShapeEditorPage final : public app::UIEditorPage
    {
    public:
        CollisionShapeEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                 ui::runtime::UIHost& uiHost,
                                 foundation::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

        // 3D preview: draw the cooked outline wireframe, drive the orbit camera, hot-swap on re-cook.
        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;
        void OnRenderWindow(runtime::IApplicationHost&,
                            foundation::graphics::FrameContext& frame) override;
        void OnClose() override;

    private:
        void PickMesh();
        void RefreshStatus();
        void DrawOutline(); // immediate-mode wireframe of the outline triangles (per frame)
        [[nodiscard]] String MeshName(const Guid& id) const;
        [[nodiscard]] static StringView CookLabel(pipeline::CollisionCookKind kind);

        EditorContext* m_context = nullptr;
        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        String m_title;
        RefPtr<pipeline::CollisionShapeAsset> m_asset;
        RefPtr<ui::View> m_content;
        RefPtr<ui::Label> m_meshLabel;
        RefPtr<ui::Button> m_cookButton;
        RefPtr<ui::Label> m_status;

        // Preview: the shared substrate + the bound cooked product (the Proxy auto-follows re-cooks,
        // so DrawOutline always reads the current outline). m_framedCount reframes the camera when
        // the outline first appears and whenever a re-cook changes its vertex count.
        UniquePtr<PreviewViewport> m_preview;
        resource::Proxy<physics::CollisionShape> m_shape;
        usize m_framedCount = 0;
    };

    class CollisionShapeEditorPageFactory final : public IEditorPageFactory
    {
    public:
        CollisionShapeEditorPageFactory(runtime::IApplicationHost& host,
                                        ui::runtime::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost)
        {
        }

        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &pipeline::CollisionShapeAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override
        {
            return UniquePtr<EditorPage>(
                DefaultAllocator().New<CollisionShapeEditorPage>(context, *m_host, *m_uiHost,
                                                                 instance),
                DefaultAllocator());
        }

    private:
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
    };

    inline void RegisterCollisionShapeEditor(EditorContext& context,
                                             runtime::IApplicationHost& host,
                                             ui::runtime::UIHost& uiHost)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<CollisionShapeEditorPageFactory>(host, uiHost),
            DefaultAllocator()));
        if (context.Thumbnails() != nullptr)
        {
            RegisterCollisionThumbnailGenerator(*context.Thumbnails());
        }
    }
}
