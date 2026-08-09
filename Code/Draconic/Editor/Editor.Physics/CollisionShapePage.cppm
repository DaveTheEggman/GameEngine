// Draconic::EditorPhysics - the `draconic.editor.physics` module (tooling).
//
// CollisionShapeEditorPage: the bespoke authoring page for a CollisionShapeAsset. It replaces the
// generic asset form (a raw guid row) with a TYPED mesh picker (no guid string), a cook-kind toggle
// (convex hull / triangle mesh), a "Cook now" action, and a one-line status naming the source mesh.
// Save writes the asset back and requests a re-cook so a shape=Cooked body picks up the new collider.
// Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.physics;

import draconic.core;
import draconic.content;
import draconic.physics.pipeline; // CollisionShapeAsset + CollisionCookKind
import draconic.ui;
import draconic.editor.core;
import draconic.editor.app;

using namespace foundation::core;

export namespace editor
{
    namespace ui = foundation::ui;

    // Authoring page for a CollisionShapeAsset (pick a mesh, choose the cook, cook it).
    class CollisionShapeEditorPage final : public app::UIEditorPage
    {
    public:
        CollisionShapeEditorPage(EditorContext& context, foundation::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

    private:
        void PickMesh();
        void RefreshStatus();
        [[nodiscard]] String MeshName(const Guid& id) const;
        [[nodiscard]] static StringView CookLabel(pipeline::CollisionCookKind kind);

        EditorContext* m_context = nullptr;
        String m_title;
        RefPtr<pipeline::CollisionShapeAsset> m_asset;
        RefPtr<ui::View> m_content;
        RefPtr<ui::Label> m_meshLabel;
        RefPtr<ui::Button> m_cookButton;
        RefPtr<ui::Label> m_status;
    };

    class CollisionShapeEditorPageFactory final : public IEditorPageFactory
    {
    public:
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &pipeline::CollisionShapeAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override
        {
            return UniquePtr<EditorPage>(
                DefaultAllocator().New<CollisionShapeEditorPage>(context, instance),
                DefaultAllocator());
        }
    };

    inline void RegisterCollisionShapeEditor(EditorContext& context)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<CollisionShapeEditorPageFactory>(), DefaultAllocator()));
    }
}
