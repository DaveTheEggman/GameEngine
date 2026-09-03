// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :material_page partition.
//
// MaterialEditorPage (Sedulous MaterialEditorPage shape): edits a MaterialAsset - a preview
// sphere lit by a default sun + procedural sky on the left, the material's parameters on the
// right. The authored form IS the runtime source (MaterialSource), so Save just writes the
// object back and requests a re-cook; every live proxy bound to the cooked product then
// hot-swaps (the same reload path texture/mesh edits ride).
//
// Edits are BLOB-SNAPSHOT commands: each edit captures the whole serialized MaterialSource
// before/after (it is tiny) - robust against the source's parallel-array layout, exact undo,
// and consecutive scrubs of the same field merge into one entry. Every apply rebuilds the
// preview's runtime material in place, so scrubbing reads live on the sphere.
//
// RegisterMaterialEditor is the module's RegisterEditor entry point: registers the
// MaterialAsset page factory and the "PBR Material" / "Unlit Material" creators (presets).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.scene:material_page;

import foundation.core;
import foundation.vfs;
import foundation.settings;
import foundation.content;
import foundation.rhi;
import foundation.graphics;
import foundation.shell;
import foundation.runtime;
import foundation.runtime.client;
import foundation.scene;
import engine.scene;
import foundation.geometry;
import foundation.materials;
import foundation.materials.resource;
import materials.pipeline;
import foundation.texture.resource;
import foundation.resource;
import foundation.shaders;
import foundation.render;
import engine.render;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import foundation.ui.viewport;
import foundation.vg.renderer;
import editor.core;
import editor.app;
import editor.preview; // PreviewViewport (shared viewport + preview scene + camera + render loop)
// NO :inspector import here: the INTERFACE names nothing from it (ResourceRefEditor is
// an impl concern - MaterialPageImpl.cpp imports :inspector itself). Beyond hygiene, it
// is load-bearing: GCC 15's module merger SEGFAULTS when an interface unit imports both
// a giant same-module partition (:inspector) and editor.preview. Keep heavy partition
// imports in impl units unless the interface actually uses their types.

using namespace foundation::core;
namespace rhi = foundation::rhi;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace vg = foundation::vg;
    namespace scene = foundation::scene;
    namespace render = foundation::render;
    namespace materials = foundation::materials;

    class MaterialEditorPage final : public app::UIEditorPage
    {
    public:
        MaterialEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                           ui::runtime::UIHost& uiHost, foundation::content::Instance& instance);
        ~MaterialEditorPage();

        // === UIEditorPage ===

        [[nodiscard]] foundation::ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }

        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;

        void OnRenderWindow(runtime::IApplicationHost&,
                            foundation::graphics::FrameContext& frame) override;

        [[nodiscard]] Status Save() override;

        void OnClose() override;

        // Deserialize a source blob into the live asset + refresh the preview (the command
        // stack's apply path - Execute and Undo both land here).
        void ApplySourceBlob(const Array<byte>& blob);

        [[nodiscard]] Array<byte> SnapshotSource() const;

    private:
        // Whole-source snapshot command: before/after blobs + a merge key (consecutive scrubs
        // of the same field collapse; the FIRST command keeps the original `before`).
        class EditMaterialCommand final : public IEditorCommand
        {
        public:
            EditMaterialCommand(MaterialEditorPage& page, StringView mergeKey, Array<byte> before,
                                Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after))
            {
            }

            [[nodiscard]] bool Execute() override
            {
                m_page->ApplySourceBlob(m_after);
                return true;
            }
            void Undo() override { m_page->ApplySourceBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_material"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditMaterialCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            MaterialEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

        // Run one edit as an undoable command: snapshot -> mutate -> snapshot -> push.
        void ApplyEdit(StringView mergeKey, Function<void(materials::MaterialSource&)> mutate);

        // Preview world: the material sphere + the standard seeded sun + default environment
        // (the render subsystem injects EnvironmentSystem on CreateScene - procedural sky/IBL).
        void BuildPreviewScene();

        // Build the runtime preview Material from the CURRENT source (the factory's conversion,
        // with textures resolved through the editor's cooked-DB resources) and swap it onto the
        // sphere. Runs on every applied edit, so scrubs read live.
        void RebuildPreviewMaterial();

        // === the parameter grid ===

        // Swap the preview geometry: a built-in primitive, or any mesh asset from the
        // project (imported models show the material with their real UVs).
        // === Preview prefs (a section in the per-project editor-settings store) ===
        // Editor state, NOT on the MaterialAsset: the preview choice is a per-user pref,
        // never a build input (the Sedulous editor kept these in its asset-cache sidecar).
        // Lives in EditorContext::ProjectEditorSettings() alongside layout/favorites/pages.

        void LoadPreviewPref();

        void SavePreviewPref();

        void ApplyPreviewMesh();

        // Reframe the fly camera to fit `mesh` (asset meshes vary wildly in size).
        void FramePreview(const foundation::geometry::StaticMesh* mesh);

        void RebuildGrid();

        // A pipeline-state dropdown writing one of the source's u8 mode fields.
        void AddPipelineEnumRow(StringView label, Span<const StringView> items,
                                u8& (*field)(materials::MaterialSource&));

        // A texture-slot picker row (TextureAsset picker; [Clear] unbinds the slot).
        void AddTextureRow(const String& slot);

        [[nodiscard]] foundation::ui::UIContext* Context() const noexcept { return m_grid->Context; }

        [[nodiscard]] StringView AssetNameFor(const Guid& target);

        void AddEditor(foundation::ui::toolkit::PropertyEditor* editor, Function<void()> refresher);

        // Uniform blob access by property name (offset/size from the source's tables).
        void ReadUniform(StringView name, void* out, usize bytes) const;
        void WriteUniform(StringView name, const void* value, usize bytes);

        EditorContext* m_context;
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
        String m_title;

        RefPtr<pipeline::MaterialAsset> m_asset;

        UniquePtr<PreviewViewport> m_preview; // shared viewport + preview scene + camera
        scene::EntityHandle m_sphere;         // the preview shape entity (in m_preview->Scene())
        RefPtr<foundation::geometry::StaticMesh> m_previewMesh;
        RefPtr<materials::Material> m_previewMaterial;
        u32 m_previewShape = 0; // index into the Shape enum row
        Guid m_previewMeshGuid; // nil = primitive shape
        Array<foundation::resource::Proxy<foundation::texture::Texture>> m_previewTextures;
        Array<rhi::TextureView*> m_previewTextureViews; // views captured into the material

        RefPtr<foundation::ui::toolkit::PropertyGrid> m_grid;
        RefPtr<foundation::ui::toolkit::SplitView> m_content;
        Array<Function<void()>> m_refreshers;
    };

    class MaterialEditorPageFactory final : public IEditorPageFactory
    {
    public:
        MaterialEditorPageFactory(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost)
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

    // Create a preset material instance in `group` (or Materials/ from the File menu).
    inline foundation::content::Instance*
    CreateMaterialInstance(EditorContext& context, foundation::content::Group* group, bool unlit)
    {
        if (context.Project() == nullptr)
        {
            return nullptr;
        }
        foundation::content::Group* target = group;
        if (target == nullptr)
        {
            foundation::content::Group* root = context.Project()->SourceDb().RootGroup();
            target = root->GetGroup(u8"Materials");
            if (target == nullptr)
            {
                target = root->CreateGroup(u8"Materials");
            }
        }
        if (target == nullptr)
        {
            return nullptr;
        }

        const String name = target->UniqueInstanceName(u8"Material");

        foundation::content::Instance* instance =
            target->CreateInstance(name.AsView(), pipeline::MaterialAsset::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }

        RefPtr<materials::Material> built =
            unlit ? materials::CreateUnlit(name.AsView()) : materials::CreatePBR(name.AsView());
        pipeline::MaterialAsset asset;
        pipeline::MaterialImporter::Import(*built, Guid{}, asset);
        if (!instance->WriteObject(asset).IsOk())
        {
            return nullptr;
        }
        LOG_INFO(u8"Editor", u8"created {} material '{}'", unlit ? u8"unlit" : u8"PBR",
                          instance->Path());
        context.RequestCook(false); // pickable as soon as the product lands
        return instance;
    }

    // Per-asset material-preview prefs: {assetGuid -> (shape, meshGuid)} - a section in the
    // per-project editor-settings store (rewritten whole; the page reads/writes its row).
    struct MaterialPreviewPref
    {
        Guid asset;
        u32 shape = 0;
        Guid mesh;

        void Serialize(ISerializer& ar)
        {
            ar.Key("asset");
            ar.GuidValue(asset);
            foundation::core::Serialize(ar, "shape", shape);
            ar.Key("mesh");
            ar.GuidValue(mesh);
        }
    };

    inline void Serialize(ISerializer& ar, MaterialPreviewPref& p)
    {
        ar.BeginObject();
        p.Serialize(ar);
        ar.EndObject();
    }

    class MaterialPreviewSettings final : public ISerializable
    {
        RTTI_OBJECT(MaterialPreviewSettings, ISerializable)
    public:
        Array<MaterialPreviewPref> prefs;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "prefs", prefs);
        }
    };

    inline void RegisterMaterialEditor(EditorContext& context, runtime::IApplicationHost& host,
                                       ui::runtime::UIHost& uiHost)
    {
        GlobalTypeRegistry().Register(pipeline::MaterialAsset::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<pipeline::MaterialAsset>();
        // The preview-prefs section (registered before the app loads the per-project store).
        GlobalTypeRegistry().Register(MaterialPreviewSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<MaterialPreviewSettings>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            foundation::core::DefaultAllocator().New<MaterialEditorPageFactory>(host, uiHost), foundation::core::DefaultAllocator()));

        EditorContext::AssetCreator pbr;
        pbr.label = String(u8"PBR Material");
        pbr.category = String(u8"Materials");
        pbr.create = [](EditorContext& ctx, foundation::content::Group* group)
        { return CreateMaterialInstance(ctx, group, /*unlit*/ false); };
        context.RegisterCreator(Move(pbr));

        EditorContext::AssetCreator unlit;
        unlit.label = String(u8"Unlit Material");
        unlit.category = String(u8"Materials");
        unlit.create = [](EditorContext& ctx, foundation::content::Group* group)
        { return CreateMaterialInstance(ctx, group, /*unlit*/ true); };
        context.RegisterCreator(Move(unlit));
    }

    RTTI_DEFINE_OBJECT_VERSIONED(MaterialPreviewSettings, "rtti::editor::editor", 1)
}
