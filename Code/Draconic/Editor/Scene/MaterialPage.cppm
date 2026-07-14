// Draconic::EditorScene - :material_page partition.
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
// RegisterMaterialEditor is the module's RegisterEditor entry point (§3.1): registers the
// MaterialAsset page factory and the "PBR Material" / "Unlit Material" creators (presets;
// custom shader-backed materials come later with the shader-asset story).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.scene:material_page;

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.rhi;
import draconic.graphics;
import draconic.shell;
import draconic.runtime;
import draconic.runtime.client;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.geometry;
import draconic.materials;
import draconic.materials.resource;
import draconic.materials.editor;
import draconic.texture.resource;
import draconic.resource;
import draconic.shaders;
import draconic.render;
import draconic.render.subsystem;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.ui.viewport;
import draconic.vg.renderer;
import draconic.editor.core;
import draconic.editor.app;
import :camera;      // EditorCamera (fly camera on the preview viewport)
import :inspector;   // ResourceRefEditor (the picker row)

using namespace draconic::core;

export namespace draconic::editor
{
    namespace rt = draconic::runtime;
    namespace uirt = draconic::ui::runtime;
    namespace uivp = draconic::ui::viewport;
    namespace vgr = draconic::vg::renderer;
    namespace dscene = draconic::scene;
    namespace drender = draconic::render;
    namespace mats = draconic::materials;

    class MaterialEditorPage final : public app::UIEditorPage
    {
    public:
        MaterialEditorPage(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost,
                           draconic::content::Instance& instance)
            : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
        {
            // Fly camera on the preview viewport (hover/focus-gated devices, like scene pages).
            m_router = MakeUnique<draconic::shell::InputRouter>(DefaultAllocator(), host.Shell()->Input());
            m_camera.position = Float3{ 0.0f, 0.9f, 2.6f };
            m_camera.LookAt(Float3{ 0.0f, 0.0f, 0.0f });

            // The edited object: the instance's MaterialAsset (kept live; Save writes it back).
            RefPtr<ISerializable> object = instance.ReadObject();
            m_asset = RefPtr<mats::MaterialAsset>(Cast<mats::MaterialAsset>(object.Get()));
            if (m_asset.Get() != nullptr)
            {
                // Pre-emissive assets gain the factor in memory (black default); saving the
                // page persists the upgraded table (the load-time upgrade covers unsaved ones).
                mats::UpgradeForwardMaterialSource(m_asset->source);
            }
            if (m_asset.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Editor", u8"material '{}' failed to read - page opens empty", m_title);
            }

            m_scenes = host.Ctx().GetSubsystem<dscene::SceneSubsystem>();
            m_render = host.Ctx().GetSubsystem<drender::RenderSubsystem>();

            BuildPreviewScene();

            // Restore this material's saved preview choice (shape or mesh asset) before the
            // grid builds its rows, so the Shape/Mesh rows show the persisted state.
            LoadPreviewPref();
            if (m_previewShape != 0 || !m_previewMeshGuid.IsNil()) { ApplyPreviewMesh(); }

            m_viewport = MakeRef<uivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.10f, 0.11f, 0.13f, 1.0f };

            m_grid = MakeRef<draconic::ui::toolkit::PropertyGrid>(DefaultAllocator());
            RebuildGrid();

            m_content = MakeRef<draconic::ui::toolkit::SplitView>(DefaultAllocator());
            m_content->SetSplitRatio(0.62f);
            m_content->SetPanes(m_viewport.Get(), m_grid.Get());

            RebuildPreviewMaterial();
        }

        // === UIEditorPage ===

        [[nodiscard]] draconic::ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }

        void OnUpdate(rt::IApplicationHost&, f32 dt) override
        {
            EnsureViewportBound();
            if (m_hostWindow == nullptr) { return; }
            m_viewport->SyncInputRegion();
            if (m_router) { m_router->Update(); }
            // Fly camera on the gated viewport devices (was an automatic turntable; user asked
            // for direct control - same navigation as the scene pages).
            if (m_viewport->IsHovered() || m_viewport->IsFocused())
            {
                m_camera.Update(m_viewport->Keyboard(), m_viewport->Mouse(), dt);
            }

            // Texture hot-reload watchdog: rebuild the preview material when any bound
            // texture's live view no longer matches what the material captured.
            for (usize i = 0; i < m_previewTextures.Size(); ++i)
            {
                rhi::TextureView* live = m_previewTextures[i] ? m_previewTextures[i]->View() : nullptr;
                if (live != m_previewTextureViews[i]) { RebuildPreviewMaterial(); break; }
            }

            for (const Function<void()>& refresher : m_refreshers) { refresher(); }
        }

        void OnRenderWindow(rt::IApplicationHost&, draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid) { return; }
            if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0 || !m_viewport->IsEffectivelyVisible()) { return; }

            drender::ViewCamera camera;
            camera.view = Float4x4::LookAtRH(m_camera.position,
                                             m_camera.position + m_camera.Forward(), m_camera.Up());
            camera.projection = Float4x4::PerspectiveFovRH(
                1.0472f, static_cast<f32>(w) / static_cast<f32>(h), 0.1f, 100.0f);
            camera.position = m_camera.position;
            camera.farZ = 100.0f;

            drender::CameraOverride cameraOverride;
            cameraOverride.camera = camera;
            cameraOverride.clearColor = Color{ m_viewport->ClearColor.r, m_viewport->ClearColor.g,
                                               m_viewport->ClearColor.b, m_viewport->ClearColor.a };

            drender::TargetState targetState;
            targetState.texture = m_viewport->ColorTexture();
            targetState.currentState = m_viewport->ColorState();
            targetState.finalState = rhi::ResourceState::ShaderRead;

            m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(), w, h,
                                  drender::ViewportRect{ 0, 0, w, h }, &cameraOverride, targetState);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        [[nodiscard]] Status Save() override
        {
            if (m_asset.Get() == nullptr || m_context->Project() == nullptr) { return Status{ ErrorCode::NotFound }; }
            draconic::content::Instance* instance = m_context->Project()->SourceDb().GetInstance(InstanceId());
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
            const Status saved = instance->WriteObject(*m_asset);
            if (saved.IsOk())
            {
                ClearDirty();
                // Refresh the cooked product so every scene's proxy hot-swaps to the new look.
                m_context->RequestCook(false);
                DRACONIC_LOG_INFO(u8"Editor", u8"saved material '{}'", m_title);
            }
            return saved;
        }

        void OnClose() override
        {
            m_viewport->Shutdown();
            if (m_scenes != nullptr && m_scene != nullptr)
            {
                m_scenes->DestroyScene(m_scene);
                m_scene = nullptr;
            }
        }

        // Deserialize a source blob into the live asset + refresh the preview (the command
        // stack's apply path - Execute and Undo both land here).
        void ApplySourceBlob(const Array<byte>& blob)
        {
            if (m_asset.Get() == nullptr) { return; }
            MemoryStream stream;
            (void)stream.Write(blob.Data(), blob.Size());
            (void)stream.Seek(0, SeekOrigin::Begin);
            BinarySerializer ar(stream, SerializeMode::Read);
            m_asset->source.Serialize(ar);
            RebuildPreviewMaterial();
        }

        [[nodiscard]] Array<byte> SnapshotSource() const
        {
            Array<byte> blob;
            if (m_asset.Get() == nullptr) { return blob; }
            MemoryStream stream;
            BinarySerializer ar(stream, SerializeMode::Write);
            const_cast<mats::MaterialSource&>(m_asset->source).Serialize(ar);
            const Span<const byte> bytes = stream.Bytes();
            blob.Reserve(bytes.Size());
            for (byte b : bytes) { blob.PushBack(b); }
            return blob;
        }

    private:
        // Whole-source snapshot command: before/after blobs + a merge key (consecutive scrubs
        // of the same field collapse; the FIRST command keeps the original `before`).
        class EditMaterialCommand final : public IEditorCommand
        {
        public:
            EditMaterialCommand(MaterialEditorPage& page, StringView mergeKey,
                                Array<byte> before, Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after)) {}

            [[nodiscard]] bool Execute() override { m_page->ApplySourceBlob(m_after); return true; }
            void Undo() override { m_page->ApplySourceBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_material"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditMaterialCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView()) { return false; }
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
        void ApplyEdit(StringView mergeKey, Function<void(mats::MaterialSource&)> mutate)
        {
            if (m_asset.Get() == nullptr) { return; }
            Array<byte> before = SnapshotSource();
            mutate(m_asset->source);
            Array<byte> after = SnapshotSource();
            // The mutation already ran; Execute() re-applies `after` (idempotent).
            (void)Commands().Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<EditMaterialCommand>(*this, mergeKey, Move(before), Move(after)),
                DefaultAllocator()));
        }

        // Preview world: the material sphere + the standard seeded sun + default environment
        // (the render subsystem injects EnvironmentSystem on CreateScene - procedural sky/IBL).
        void BuildPreviewScene()
        {
            if (m_scenes == nullptr) { return; }
            m_scene = m_scenes->CreateScene(u8"material.preview");
            m_scene->SetSimulationEnabled(false);

            m_sphere = m_scene->CreateEntity(u8"PreviewSphere");
            m_previewMesh = draconic::geometry::Primitives::Sphere(1.0f, 48, 24);
            if (auto* meshes = m_scene->GetSystem<drender::MeshComponentManager>())
            {
                drender::MeshComponent& mc = meshes->Add(m_sphere);
                mc.mesh = m_previewMesh.Get();   // direct override (runtime-built, not an asset)
            }

            const dscene::EntityHandle sun = m_scene->CreateEntity(u8"Sun");
            Transform t;
            t.rotation = Quaternion::FromAxisAngle(Float3{ 0, 1, 0 }, 0.35f)
                       * Quaternion::FromAxisAngle(Float3{ 1, 0, 0 }, -1.05f);
            m_scene->SetLocalTransform(sun, t);
            if (auto* lights = m_scene->GetSystem<drender::LightComponentManager>())
            {
                drender::LightComponent& light = lights->Add(sun);
                light.castsShadows = false;   // a lone sphere has nothing to shadow
            }
        }

        // Build the runtime preview Material from the CURRENT source (the factory's conversion,
        // with textures resolved through the editor's cooked-DB resources) and swap it onto the
        // sphere. Runs on every applied edit, so scrubs read live.
        void RebuildPreviewMaterial()
        {
            if (m_asset.Get() == nullptr || m_scene == nullptr || !m_sphere.IsAssigned()) { return; }
            const mats::MaterialSource& src = m_asset->source;

            RefPtr<mats::Material> material = MakeRef<mats::Material>(DefaultAllocator());
            material->name = String(src.name.AsView());
            material->shaderName = String(src.shaderName.AsView());
            material->shaderFlags = static_cast<draconic::shaders::ShaderFlags>(src.shaderFlags);
            for (usize i = 0; i < src.propNames.Size(); ++i)
            {
                mats::MaterialPropertyDef d{};
                d.name    = src.propNames[i].AsView();
                d.type    = static_cast<mats::MaterialPropertyType>(src.propTypes[i]);
                d.binding = (i < src.propBindings.Size()) ? src.propBindings[i] : 0u;
                d.offset  = (i < src.propOffsets.Size())  ? src.propOffsets[i]  : 0u;
                d.size    = (i < src.propSizes.Size())    ? src.propSizes[i]    : 0u;
                material->AddProperty(d);
            }
            material->AllocateDefaultUniformData();
            material->SetRawDefaultUniformData(Span<const u8>{ src.uniformDefaults.Data(), src.uniformDefaults.Size() });
            material->pipeline = mats::PipelineConfig{};
            material->pipeline.shaderName   = material->shaderName.AsView();
            material->pipeline.shaderFlags  = material->shaderFlags;
            material->pipeline.blendMode    = static_cast<mats::BlendMode>(src.blendMode);
            material->pipeline.depthMode    = static_cast<mats::DepthMode>(src.depthMode);
            material->pipeline.cullMode     = static_cast<mats::CullModeConfig>(src.cullMode);
            material->pipeline.vertexLayout = static_cast<mats::VertexLayoutType>(src.vertexLayout);

            m_previewTextures.Clear();
            m_previewTextureViews.Clear();
            if (m_context->Resources() != nullptr)
            {
                for (usize i = 0; i < src.textureSlots.Size() && i < src.textureIds.Size(); ++i)
                {
                    if (src.textureIds[i].IsNil()) { continue; }
                    draconic::resource::Proxy<draconic::texture::Texture> tex =
                        m_context->Resources()->Bind<draconic::texture::Texture>(src.textureIds[i]);
                    // Track the proxy + the RAW view captured into the material: a texture
                    // hot-reload (cook) destroys that view under us, so OnUpdate watches for
                    // the proxy's view changing and rebuilds the preview material (otherwise
                    // set-2 keeps a dangling descriptor - null-imageView validation errors).
                    m_previewTextures.PushBack(tex);
                    m_previewTextureViews.PushBack(tex ? tex->View() : nullptr);
                    if (tex && tex->View() != nullptr)
                    {
                        material->SetDefaultTexture(src.textureSlots[i].AsView(), tex->View());
                    }
                }
            }

            m_previewMaterial = material;
            if (auto* meshes = m_scene->GetSystem<drender::MeshComponentManager>())
            {
                if (drender::MeshComponent* mc = meshes->Get(m_sphere))
                {
                    mc->material = m_previewMaterial.Get();   // direct override
                }
            }
        }

        // === the parameter grid ===

        // Swap the preview geometry: a built-in primitive, or any mesh asset from the
        // project (imported models show the material with their real UVs).
        // === Preview prefs persistence (<project>/Editor/material-preview.bin) ===
        // Editor state, NOT on the MaterialAsset: the preview choice is a per-user pref,
        // never a build input (the Sedulous editor kept these in its asset-cache sidecar).
        // Flat map: count + { assetGuid, shape u32, meshGuid } records, rewritten whole.

        struct PreviewPref { Guid asset; u32 shape = 0; Guid mesh; };

        [[nodiscard]] static String PreviewPrefsPath(EditorContext& context)
        {
            return (context.Project() != nullptr) ? context.Project()->EditorStateRoot() : String();
        }

        static void LoadPreviewPrefs(EditorContext& context, Array<PreviewPref>& out)
        {
            const String dir = PreviewPrefsPath(context);
            if (dir.IsEmpty()) { return; }
            draconic::vfs::NativeFileSystem root(dir.AsView());
            UniquePtr<IStream> stream = root.Open(u8"material-preview.bin", FileMode::Read);
            if (!stream) { return; }
            BinarySerializer ar(*stream, SerializeMode::Read);
            u32 count = 0;
            draconic::core::Serialize(ar, "count", count);
            for (u32 i = 0; i < count && ar.IsOk(); ++i)
            {
                PreviewPref pref;
                ar.Key("asset"); ar.GuidValue(pref.asset);
                draconic::core::Serialize(ar, "shape", pref.shape);
                ar.Key("mesh"); ar.GuidValue(pref.mesh);
                out.PushBack(pref);
            }
            if (!ar.IsOk()) { out.Clear(); }
        }

        static void SavePreviewPrefs(EditorContext& context, Span<const PreviewPref> prefs)
        {
            const String dir = PreviewPrefsPath(context);
            if (dir.IsEmpty()) { return; }
            MemoryStream buffer;
            BinarySerializer ar(buffer, SerializeMode::Write);
            u32 count = static_cast<u32>(prefs.Size());
            draconic::core::Serialize(ar, "count", count);
            for (const PreviewPref& p : prefs)
            {
                PreviewPref copy = p;
                ar.Key("asset"); ar.GuidValue(copy.asset);
                draconic::core::Serialize(ar, "shape", copy.shape);
                ar.Key("mesh"); ar.GuidValue(copy.mesh);
            }
            if (!ar.IsOk()) { return; }
            draconic::vfs::NativeFileSystem root(dir.AsView());
            if (draconic::vfs::IWritableFileSystem* writable = root.AsWritable())
            {
                (void)writable->Save(u8"material-preview.bin", buffer.Bytes());
            }
        }

        void LoadPreviewPref()
        {
            Array<PreviewPref> prefs;
            LoadPreviewPrefs(*m_context, prefs);
            for (const PreviewPref& p : prefs)
            {
                if (p.asset == InstanceId())
                {
                    m_previewShape = p.shape;
                    m_previewMeshGuid = p.mesh;
                    return;
                }
            }
        }

        void SavePreviewPref()
        {
            Array<PreviewPref> prefs;
            LoadPreviewPrefs(*m_context, prefs);
            bool found = false;
            for (PreviewPref& p : prefs)
            {
                if (p.asset == InstanceId())
                {
                    p.shape = m_previewShape;
                    p.mesh = m_previewMeshGuid;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                prefs.PushBack(PreviewPref{ InstanceId(), m_previewShape, m_previewMeshGuid });
            }
            SavePreviewPrefs(*m_context, Span<const PreviewPref>{ prefs.Data(), prefs.Size() });
        }

        void ApplyPreviewMesh()
        {
            if (m_scene == nullptr || !m_sphere.IsAssigned()) { return; }
            auto* meshes = m_scene->GetSystem<drender::MeshComponentManager>();
            drender::MeshComponent* mc = (meshes != nullptr) ? meshes->Get(m_sphere) : nullptr;
            if (mc == nullptr) { return; }

            if (!m_previewMeshGuid.IsNil() && m_context->Resources() != nullptr)
            {
                m_previewMesh = nullptr;
                mc->mesh.SetDirect(core::RefPtr<draconic::geometry::StaticMesh>{});
                mc->mesh.SetId(m_previewMeshGuid);
                mc->mesh.Bind(*m_context->Resources());
                FramePreview(mc->mesh.Get());
                return;
            }

            namespace geo = draconic::geometry;
            switch (m_previewShape)
            {
                case 1:  m_previewMesh = geo::Primitives::Cube(1.4f); break;
                case 2:  m_previewMesh = geo::Primitives::Plane(2.0f, 2.0f); break;
                case 3:  m_previewMesh = geo::Primitives::Cylinder(0.7f, 1.6f, 48); break;
                case 4:  m_previewMesh = geo::Primitives::Torus(0.8f, 0.35f, 48, 24); break;
                case 5:  m_previewMesh = geo::Primitives::Cone(0.8f, 1.6f, 48); break;
                default: m_previewMesh = geo::Primitives::Sphere(1.0f, 48, 24); break;
            }
            mc->mesh.SetId(Guid{});
            mc->mesh = m_previewMesh.Get();   // direct override (runtime-built, not an asset)
            FramePreview(m_previewMesh.Get());
        }

        // Reframe the fly camera to fit `mesh` (asset meshes vary wildly in size).
        void FramePreview(const draconic::geometry::StaticMesh* mesh)
        {
            f32 radius = 1.0f;
            Float3 center{ 0.0f, 0.0f, 0.0f };
            if (mesh != nullptr && mesh->VertexCount() > 0)
            {
                center = mesh->bounds.Center();
                radius = core::Max(0.25f, Length(mesh->bounds.Extents()));
            }
            m_camera.position = center + Float3{ 0.0f, 0.35f, 1.0f } * (radius * 2.4f);
            m_camera.LookAt(center);
        }

        void RebuildGrid()
        {
            m_grid->Clear();
            m_refreshers.Clear();
            if (m_asset.Get() == nullptr) { return; }
            namespace tk = draconic::ui::toolkit;
            MaterialEditorPage* self = this;
            const mats::MaterialSource& src = m_asset->source;

            // --- Material: shader + pipeline state ---
            const StringView shaderShown = src.shaderName.IsEmpty() ? StringView(u8"(shader asset)")
                                                                    : src.shaderName.AsView();
            auto shader = MakeRef<tk::StringEditor>(DefaultAllocator(), StringView(u8"Shader"),
                shaderShown, Function<void(StringView)>{}, StringView(u8"Material"));
            m_grid->AddProperty(RefPtr<tk::PropertyEditor>(shader.Get()));

            // --- Preview: geometry the material is shown on (page-local, not saved) ---
            {
                static constexpr StringView kShapes[] = { u8"Sphere", u8"Cube", u8"Plane",
                                                          u8"Cylinder", u8"Torus", u8"Cone" };
                auto shape = MakeRef<tk::EnumEditor>(DefaultAllocator(), StringView(u8"Shape"),
                    static_cast<i32>(m_previewShape), Span<const StringView>{ kShapes, 6 },
                    Function<void(i32)>{ [self](i32 index) {
                        self->m_previewShape = static_cast<u32>(core::Max(0, index));
                        self->m_previewMeshGuid = Guid{};   // shape picks override an asset mesh
                        self->ApplyPreviewMesh();
                        self->SavePreviewPref();
                    } },
                    StringView(u8"Preview"));
                m_grid->AddProperty(RefPtr<tk::PropertyEditor>(shape.Get()));

                auto meshName = [self]() -> StringView {
                    if (self->m_previewMeshGuid.IsNil()) { return u8"(primitive)"; }
                    if (self->m_context->Project() != nullptr)
                    {
                        if (draconic::content::Instance* inst =
                                self->m_context->Project()->SourceDb().GetInstance(self->m_previewMeshGuid))
                        {
                            return inst->Name();
                        }
                    }
                    return u8"(missing)";
                };
                auto meshRow = MakeRef<ResourceRefEditor>(DefaultAllocator(), StringView(u8"Mesh"),
                                                          meshName(), StringView(u8"Preview"));
                ResourceRefEditor* meshRaw = meshRow.Get();
                meshRow->OnPick = [self, meshRaw, meshName]() {
                    if (self->m_content->Context == nullptr || self->m_context->Project() == nullptr) { return; }
                    Array<String> typeNames;
                    typeNames.PushBack(String(u8"StaticMeshAsset"));
                    typeNames.PushBack(String(u8"SkinnedMeshAsset"));
                    auto dialog = MakeRef<draconic::editor::app::AssetPickerDialog>(
                        DefaultAllocator(), *self->m_context, Move(typeNames));
                    dialog->OnPicked = [self, meshRaw, meshName](const Guid& picked) {
                        self->m_previewMeshGuid = picked;   // nil (Clear) = back to the primitive
                        self->ApplyPreviewMesh();
                        self->SavePreviewPref();
                        meshRaw->SetValueText(meshName());
                    };
                    dialog->Show(self->m_content->Context);
                };
                m_grid->AddProperty(RefPtr<tk::PropertyEditor>(meshRow.Get()));
            }

            static constexpr StringView kBlendItems[] = { u8"Opaque", u8"Masked", u8"AlphaBlend",
                                                          u8"Additive", u8"Multiply", u8"PremultipliedAlpha" };
            static constexpr StringView kDepthItems[] = { u8"Disabled", u8"ReadWrite", u8"ReadOnly", u8"WriteOnly" };
            static constexpr StringView kCullItems[]  = { u8"None", u8"Back", u8"Front" };
            AddPipelineEnumRow(u8"Blend", Span<const StringView>{ kBlendItems, 6 },
                               [](mats::MaterialSource& s) -> u8& { return s.blendMode; });
            AddPipelineEnumRow(u8"Depth", Span<const StringView>{ kDepthItems, 4 },
                               [](mats::MaterialSource& s) -> u8& { return s.depthMode; });
            AddPipelineEnumRow(u8"Cull", Span<const StringView>{ kCullItems, 3 },
                               [](mats::MaterialSource& s) -> u8& { return s.cullMode; });

            // --- Properties: the source's uniform table (Float / Float4-as-color today) ---
            for (usize i = 0; i < src.propNames.Size(); ++i)
            {
                const auto type = static_cast<mats::MaterialPropertyType>(src.propTypes[i]);
                const String name = src.propNames[i];   // copy: the grid outlives rebuilds of src arrays
                if (type == mats::MaterialPropertyType::Float)
                {
                    auto value = [self, name]() -> f64 {
                        f32 v = 0.0f;
                        self->ReadUniform(name.AsView(), &v, sizeof(v));
                        return static_cast<f64>(v);
                    };
                    auto editor = MakeRef<tk::FloatEditor>(DefaultAllocator(), name.AsView(), value(),
                        0.0, 1e9, 0.01, 3,
                        Function<void(f64)>{ [self, name](f64 v) {
                            self->ApplyEdit(name.AsView(), Function<void(mats::MaterialSource&)>{
                                [self, name, v](mats::MaterialSource&) {
                                    const f32 f = static_cast<f32>(v);
                                    self->WriteUniform(name.AsView(), &f, sizeof(f));
                                } });
                        } },
                        StringView(u8"Properties"));
                    AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                }
                else if (type == mats::MaterialPropertyType::Float4)
                {
                    auto value = [self, name]() -> Color {
                        Float4 v{ 1, 1, 1, 1 };
                        self->ReadUniform(name.AsView(), &v, sizeof(v));
                        return Color{ v.x, v.y, v.z, v.w };
                    };
                    auto editor = MakeRef<tk::ColorEditor>(DefaultAllocator(), name.AsView(), value(),
                        Function<void(Color)>{ [self, name](Color c) {
                            self->ApplyEdit(name.AsView(), Function<void(mats::MaterialSource&)>{
                                [self, name, c](mats::MaterialSource&) {
                                    const Float4 v{ c.r, c.g, c.b, c.a };
                                    self->WriteUniform(name.AsView(), &v, sizeof(v));
                                } });
                        } },
                        StringView(u8"Properties"));
                    AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                }
                else if (type == mats::MaterialPropertyType::Texture2D
                         || type == mats::MaterialPropertyType::TextureCube)
                {
                    AddTextureRow(name);
                }
                // Sampler/Int/Matrix rows: extend when a preset needs them.
            }
        }

        // A pipeline-state dropdown writing one of the source's u8 mode fields.
        void AddPipelineEnumRow(StringView label, Span<const StringView> items,
                                u8& (*field)(mats::MaterialSource&))
        {
            namespace tk = draconic::ui::toolkit;
            MaterialEditorPage* self = this;
            auto read = [self, field]() -> i32 {
                return (self->m_asset.Get() != nullptr)
                    ? static_cast<i32>(field(self->m_asset->source)) : 0;
            };
            const String key(label);
            auto editor = MakeRef<tk::EnumEditor>(DefaultAllocator(), label, read(), items,
                Function<void(i32)>{ [self, field, key](i32 index) {
                    self->ApplyEdit(key.AsView(), Function<void(mats::MaterialSource&)>{
                        [field, index](mats::MaterialSource& s) {
                            field(s) = static_cast<u8>(index);
                        } });
                } },
                StringView(u8"Material"));
            AddEditor(editor.Get(), [read, raw = editor.Get()]() { raw->SetValue(read()); });
        }

        // A texture-slot picker row (TextureAsset picker; [Clear] unbinds the slot).
        void AddTextureRow(const String& slot)
        {
            MaterialEditorPage* self = this;
            auto target = [self, slot]() -> Guid {
                if (self->m_asset.Get() == nullptr) { return Guid{}; }
                const mats::MaterialSource& s = self->m_asset->source;
                for (usize i = 0; i < s.textureSlots.Size() && i < s.textureIds.Size(); ++i)
                {
                    if (s.textureSlots[i].AsView() == slot.AsView()) { return s.textureIds[i]; }
                }
                return Guid{};
            };
            auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), slot.AsView(),
                AssetNameFor(target()), StringView(u8"Textures"));
            ResourceRefEditor* raw = editor.Get();
            raw->OnPick = [self, slot]() {
                if (self->Context() == nullptr || self->m_context->Project() == nullptr) { return; }
                Array<String> typeNames;
                typeNames.PushBack(String(u8"TextureAsset"));
                auto dialog = MakeRef<draconic::editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_context, Move(typeNames));
                dialog->OnPicked = [self, slot](const Guid& picked) {
                    self->ApplyEdit(slot.AsView(), Function<void(mats::MaterialSource&)>{
                        [slot, picked](mats::MaterialSource& s) {
                            for (usize i = 0; i < s.textureSlots.Size(); ++i)
                            {
                                if (s.textureSlots[i].AsView() != slot.AsView()) { continue; }
                                if (picked.IsNil())
                                {
                                    s.textureSlots.RemoveAt(i);
                                    s.textureIds.RemoveAt(i);
                                }
                                else { s.textureIds[i] = picked; }
                                return;
                            }
                            if (!picked.IsNil())
                            {
                                s.textureSlots.PushBack(String(slot.AsView()));
                                s.textureIds.PushBack(picked);
                            }
                        } });
                };
                dialog->Show(self->Context());
            };
            AddEditor(raw, [self, target, raw]() { raw->SetValueText(self->AssetNameFor(target())); });
        }

        [[nodiscard]] draconic::ui::UIContext* Context() const noexcept { return m_grid->Context; }

        [[nodiscard]] StringView AssetNameFor(const Guid& target)
        {
            if (target.IsNil()) { return u8"(none)"; }
            if (m_context->Project() != nullptr)
            {
                if (draconic::content::Instance* inst = m_context->Project()->SourceDb().GetInstance(target))
                {
                    return inst->Name();
                }
            }
            return u8"(missing)";
        }

        void AddEditor(draconic::ui::toolkit::PropertyEditor* editor, Function<void()> refresher)
        {
            m_grid->AddProperty(RefPtr<draconic::ui::toolkit::PropertyEditor>(editor));
            draconic::ui::toolkit::PropertyEditor* raw = editor;
            m_refreshers.PushBack(Function<void()>{
                [raw, pull = Move(refresher)]() {
                    if (!raw->IsEditing()) { pull(); }
                } });
        }

        // Uniform blob access by property name (offset/size from the source's tables).
        void ReadUniform(StringView name, void* out, usize bytes) const
        {
            const mats::MaterialSource& s = m_asset->source;
            for (usize i = 0; i < s.propNames.Size(); ++i)
            {
                if (s.propNames[i].AsView() != name) { continue; }
                const u32 offset = (i < s.propOffsets.Size()) ? s.propOffsets[i] : 0u;
                if (offset + bytes <= s.uniformDefaults.Size())
                {
                    MemCopy(out, s.uniformDefaults.Data() + offset, bytes);
                }
                return;
            }
        }
        void WriteUniform(StringView name, const void* value, usize bytes)
        {
            mats::MaterialSource& s = m_asset->source;
            for (usize i = 0; i < s.propNames.Size(); ++i)
            {
                if (s.propNames[i].AsView() != name) { continue; }
                const u32 offset = (i < s.propOffsets.Size()) ? s.propOffsets[i] : 0u;
                if (offset + bytes <= s.uniformDefaults.Size())
                {
                    MemCopy(s.uniformDefaults.Data() + offset, value, bytes);
                }
                return;
            }
        }

        void EnsureViewportBound()
        {
            draconic::ui::RootView* root = m_viewport->Root();
            if (root == nullptr) { return; }
            draconic::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
            if (window == nullptr || window == m_hostWindow) { return; }
            vgr::VGRenderer* renderer = m_uiHost->RendererFor(window);
            if (renderer == nullptr) { return; }
            if (m_hostWindow == nullptr)
            {
                m_viewport->Initialize(m_host->Graphics()->Raw(), renderer,
                                       m_host->Shell()->Input(), window->Window().Id());
                if (m_viewport->Surface() != nullptr) { m_router->AddSurface(m_viewport->Surface()); }
            }
            else
            {
                m_viewport->AttachToWindow(renderer, window->Window().Id());
            }
            m_hostWindow = window;
        }

        EditorContext* m_context;
        rt::IApplicationHost* m_host;
        uirt::UIHost* m_uiHost;
        String m_title;

        RefPtr<mats::MaterialAsset> m_asset;

        dscene::SceneSubsystem* m_scenes = nullptr;
        drender::RenderSubsystem* m_render = nullptr;
        dscene::Scene* m_scene = nullptr;
        dscene::EntityHandle m_sphere;
        RefPtr<draconic::geometry::StaticMesh> m_previewMesh;
        RefPtr<mats::Material> m_previewMaterial;
        EditorCamera m_camera;
        UniquePtr<draconic::shell::InputRouter> m_router;
        u32 m_previewShape = 0;      // index into the Shape enum row
        Guid m_previewMeshGuid;      // nil = primitive shape
        Array<draconic::resource::Proxy<draconic::texture::Texture>> m_previewTextures;
        Array<rhi::TextureView*> m_previewTextureViews;   // views captured into the material

        RefPtr<uivp::ViewportView> m_viewport;
        RefPtr<draconic::ui::toolkit::PropertyGrid> m_grid;
        RefPtr<draconic::ui::toolkit::SplitView> m_content;
        Array<Function<void()>> m_refreshers;
        draconic::graphics::RenderWindow* m_hostWindow = nullptr;
    };

    class MaterialEditorPageFactory final : public IEditorPageFactory
    {
    public:
        MaterialEditorPageFactory(rt::IApplicationHost& host, uirt::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost) {}

        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &mats::MaterialAsset::StaticType();
        }

        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            MaterialEditorPage* page = DefaultAllocator().New<MaterialEditorPage>(
                context, *m_host, *m_uiHost, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

    private:
        rt::IApplicationHost* m_host;
        uirt::UIHost* m_uiHost;
    };

    // Create a preset material instance in `group` (or Materials/ from the File menu).
    inline draconic::content::Instance* CreateMaterialInstance(EditorContext& context,
                                                               draconic::content::Group* group,
                                                               bool unlit)
    {
        if (context.Project() == nullptr) { return nullptr; }
        draconic::content::Group* target = group;
        if (target == nullptr)
        {
            draconic::content::Group* root = context.Project()->SourceDb().RootGroup();
            target = root->GetGroup(u8"Materials");
            if (target == nullptr) { target = root->CreateGroup(u8"Materials"); }
        }
        if (target == nullptr) { return nullptr; }

        String name(u8"Material");
        for (i32 counter = 2; target->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(u8"Material");
            if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
        }

        draconic::content::Instance* instance =
            target->CreateInstance(name.AsView(), mats::MaterialAsset::StaticType());
        if (instance == nullptr) { return nullptr; }

        RefPtr<mats::Material> built = unlit
            ? mats::CreateUnlit(name.AsView())
            : mats::CreatePBR(name.AsView());
        mats::MaterialAsset asset;
        mats::MaterialImporter::Import(*built, Guid{}, asset);
        if (!instance->WriteObject(asset).IsOk()) { return nullptr; }
        DRACONIC_LOG_INFO(u8"Editor", u8"created {} material '{}'", unlit ? u8"unlit" : u8"PBR",
                          instance->Path());
        context.RequestCook(false);   // pickable as soon as the product lands
        return instance;
    }

    inline void RegisterMaterialEditor(EditorContext& context, rt::IApplicationHost& host,
                                       uirt::UIHost& uiHost)
    {
        GlobalTypeRegistry().Register(mats::MaterialAsset::StaticType());
        RegisterSerializable<mats::MaterialAsset>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<MaterialEditorPageFactory>(host, uiHost), DefaultAllocator()));

        EditorContext::AssetCreator pbr;
        pbr.label = String(u8"PBR Material");
        pbr.category = String(u8"Materials");
        pbr.create = [](EditorContext& ctx, draconic::content::Group* group) {
            return CreateMaterialInstance(ctx, group, /*unlit*/ false);
        };
        context.RegisterCreator(Move(pbr));

        EditorContext::AssetCreator unlit;
        unlit.label = String(u8"Unlit Material");
        unlit.category = String(u8"Materials");
        unlit.create = [](EditorContext& ctx, draconic::content::Group* group) {
            return CreateMaterialInstance(ctx, group, /*unlit*/ true);
        };
        context.RegisterCreator(Move(unlit));
    }
}
