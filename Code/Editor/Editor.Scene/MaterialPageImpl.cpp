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

module editor.scene;

import foundation.core;
import foundation.vfs;
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
import editor.preview;   // PreviewViewport (shared viewport + preview scene + camera + render loop)
import :inspector; // ResourceRefEditor (the picker row)

using namespace foundation::core;
namespace core = foundation::core;
namespace materials = foundation::materials;
namespace render = foundation::render;
namespace rhi = foundation::rhi;
namespace runtime = foundation::runtime;
namespace scene = foundation::scene;
namespace ui = foundation::ui;
namespace vg = foundation::vg;

namespace editor
{
    MaterialEditorPage::MaterialEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                           ui::runtime::UIHost& uiHost,
                                           foundation::content::Instance& instance)
        : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
    {
        // The edited object: the instance's MaterialAsset (kept live; Save writes it back).
        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<pipeline::MaterialAsset>(Cast<pipeline::MaterialAsset>(object.Get()));
        if (m_asset.Get() != nullptr)
        {
            // Pre-emissive assets gain the factor in memory (black default); saving the
            // page persists the upgraded table (the load-time upgrade covers unsaved ones).
            materials::UpgradeForwardMaterialSource(m_asset->source);
        }
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"material '{}' failed to read - page opens empty", m_title);
        }

        // Shared preview substrate (viewport + preview scene + fly camera + render loop).
        m_preview =
            MakeUnique<PreviewViewport>(DefaultAllocator(), host, uiHost, u8"material.preview");
        m_preview->Camera().position = Float3{0.0f, 0.9f, 2.6f};
        m_preview->Camera().LookAt(Float3{0.0f, 0.0f, 0.0f});

        BuildPreviewScene();

        // The context assigns the page's instance id AFTER construction (OpenPage), but the
        // preview-pref restore below keys on it - set it from the instance now (the context's
        // later SetInstanceId writes the same value).
        SetInstanceId(instance.Id());

        // Restore this material's saved preview choice (shape or mesh asset) before the grid
        // builds its rows, so the Shape/Mesh rows show the persisted state.
        LoadPreviewPref();
        if (m_previewShape != 0 || !m_previewMeshGuid.IsNil())
        {
            ApplyPreviewMesh();
        }

        m_grid = MakeRef<foundation::ui::toolkit::PropertyGrid>(DefaultAllocator());
        RebuildGrid();

        // Inset the property grid off the pane edge (matches the scene inspector / hierarchy).
        auto gridColumn = MakeRef<foundation::ui::FlexLayout>(DefaultAllocator());
        gridColumn->Direction = foundation::ui::Orientation::Vertical;
        gridColumn->Padding = foundation::ui::Thickness{8, 6};
        {
            auto grow = MakeRef<foundation::ui::FlexLayoutParams>(DefaultAllocator());
            grow->Grow = 1.0f;
            gridColumn->AddView(m_grid.Get(), grow);
        }

        m_content = MakeRef<foundation::ui::toolkit::SplitView>(DefaultAllocator());
        m_content->SetSplitRatio(0.62f);
        m_content->SetPanes(m_preview->View(), gridColumn.Get());

        RebuildPreviewMaterial();
    }

    MaterialEditorPage::~MaterialEditorPage() = default;

    void MaterialEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        if (m_preview)
        {
            m_preview->Update(dt);
        }

        // Texture hot-reload watchdog: rebuild the preview material when any bound
        // texture's live view no longer matches what the material captured.
        for (usize i = 0; i < m_previewTextures.Size(); ++i)
        {
            rhi::TextureView* live = m_previewTextures[i] ? m_previewTextures[i]->View() : nullptr;
            if (live != m_previewTextureViews[i])
            {
                RebuildPreviewMaterial();
                break;
            }
        }

        for (const Function<void()>& refresher : m_refreshers)
        {
            refresher();
        }
    }

    void MaterialEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                            foundation::graphics::FrameContext& frame)
    {
        if (m_preview)
        {
            m_preview->RenderFrame(frame);
        }
    }

    Status MaterialEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            // Refresh the cooked product so every scene's proxy hot-swaps to the new look.
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved material '{}'", m_title);
        }
        return saved;
    }

    void MaterialEditorPage::OnClose()
    {
        if (m_preview)
        {
            m_preview->Shutdown();
        }
    }

    void MaterialEditorPage::ApplySourceBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        m_asset->source.Serialize(ar);
        RebuildPreviewMaterial();
    }

    Array<byte> MaterialEditorPage::SnapshotSource() const
    {
        Array<byte> blob;
        if (m_asset.Get() == nullptr)
        {
            return blob;
        }
        MemoryStream stream;
        BinarySerializer ar(stream, SerializeMode::Write);
        const_cast<materials::MaterialSource&>(m_asset->source).Serialize(ar);
        const Span<const byte> bytes = stream.Bytes();
        blob.Reserve(bytes.Size());
        for (byte b : bytes)
        {
            blob.PushBack(b);
        }
        return blob;
    }

    void MaterialEditorPage::ApplyEdit(StringView mergeKey,
                                       Function<void(materials::MaterialSource&)> mutate)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        Array<byte> before = SnapshotSource();
        mutate(m_asset->source);
        Array<byte> after = SnapshotSource();
        // The mutation already ran; Execute() re-applies `after` (idempotent).
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            DefaultAllocator().New<EditMaterialCommand>(*this, mergeKey, Move(before), Move(after)),
            DefaultAllocator()));
    }

    void MaterialEditorPage::BuildPreviewScene()
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (scenePtr == nullptr)
        {
            return;
        }

        m_sphere = scenePtr->CreateEntity(u8"PreviewSphere");
        m_previewMesh = foundation::geometry::Primitives::Sphere(1.0f, 48, 24);
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

    void MaterialEditorPage::RebuildPreviewMaterial()
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (m_asset.Get() == nullptr || scenePtr == nullptr || !m_sphere.IsAssigned())
        {
            return;
        }
        const materials::MaterialSource& src = m_asset->source;

        RefPtr<materials::Material> material = MakeRef<materials::Material>(DefaultAllocator());
        material->name = String(src.name.AsView());
        material->shaderName = String(src.shaderName.AsView());
        material->shaderFlags = static_cast<foundation::shaders::ShaderFlags>(src.shaderFlags);
        for (usize i = 0; i < src.propNames.Size(); ++i)
        {
            materials::MaterialPropertyDef d{};
            d.name = src.propNames[i].AsView();
            d.type = static_cast<materials::MaterialPropertyType>(src.propTypes[i]);
            d.binding = (i < src.propBindings.Size()) ? src.propBindings[i] : 0u;
            d.offset = (i < src.propOffsets.Size()) ? src.propOffsets[i] : 0u;
            d.size = (i < src.propSizes.Size()) ? src.propSizes[i] : 0u;
            material->AddProperty(d);
        }
        material->AllocateDefaultUniformData();
        material->SetRawDefaultUniformData(
            Span<const u8>{src.uniformDefaults.Data(), src.uniformDefaults.Size()});
        material->pipeline = materials::PipelineConfig{};
        material->pipeline.shaderName = material->shaderName.AsView();
        material->pipeline.shaderFlags = material->shaderFlags;
        material->pipeline.blendMode = static_cast<materials::BlendMode>(src.blendMode);
        material->pipeline.depthMode = static_cast<materials::DepthMode>(src.depthMode);
        material->pipeline.cullMode = static_cast<materials::CullModeConfig>(src.cullMode);
        material->pipeline.vertexLayout =
            static_cast<materials::VertexLayoutType>(src.vertexLayout);
        material->samplerU = static_cast<rhi::AddressMode>(src.samplerU);
        material->samplerV = static_cast<rhi::AddressMode>(src.samplerV);

        m_previewTextures.Clear();
        m_previewTextureViews.Clear();
        if (m_context->Resources() != nullptr)
        {
            for (usize i = 0; i < src.textureSlots.Size() && i < src.textureIds.Size(); ++i)
            {
                if (src.textureIds[i].IsNil())
                {
                    continue;
                }
                foundation::resource::Proxy<foundation::texture::Texture> tex =
                    m_context->Resources()->Bind<foundation::texture::Texture>(src.textureIds[i]);
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
        if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
        {
            if (engine::render::MeshComponent* mc = meshes->Get(m_sphere))
            {
                mc->SetMaterial(RefPtr<foundation::materials::Material>(
                    m_previewMaterial.Get())); // direct override
            }
        }
    }

    void MaterialEditorPage::LoadPreviewPref()
    {
        foundation::settings::Settings* store = m_context->ProjectEditorSettings();
        if (store == nullptr)
        {
            return;
        }
        if (const MaterialPreviewSettings* section = store->Find<MaterialPreviewSettings>())
        {
            for (const MaterialPreviewPref& p : section->prefs)
            {
                if (p.asset == InstanceId())
                {
                    m_previewShape = p.shape;
                    m_previewMeshGuid = p.mesh;
                    return;
                }
            }
        }
    }

    void MaterialEditorPage::SavePreviewPref()
    {
        foundation::settings::Settings* store = m_context->ProjectEditorSettings();
        if (store == nullptr)
        {
            return;
        }
        MaterialPreviewSettings& section = store->Section<MaterialPreviewSettings>();
        bool found = false;
        for (MaterialPreviewPref& p : section.prefs)
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
            section.prefs.PushBack(
                MaterialPreviewPref{InstanceId(), m_previewShape, m_previewMeshGuid});
        }
        store->MarkChanged<MaterialPreviewSettings>();
        m_context->RequestProjectEditorSettingsSave();
    }

    void MaterialEditorPage::ApplyPreviewMesh()
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (scenePtr == nullptr || !m_sphere.IsAssigned())
        {
            return;
        }
        auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>();
        engine::render::MeshComponent* mc = (meshes != nullptr) ? meshes->Get(m_sphere) : nullptr;
        if (mc == nullptr)
        {
            return;
        }

        if (!m_previewMeshGuid.IsNil() && m_context->Resources() != nullptr)
        {
            m_previewMesh = nullptr;
            mc->mesh.SetDirect(core::RefPtr<foundation::geometry::StaticMesh>{});
            mc->mesh.SetId(m_previewMeshGuid);
            mc->mesh.Bind(*m_context->Resources());
            FramePreview(mc->mesh.Get());
            return;
        }

        namespace geometry = foundation::geometry;
        switch (m_previewShape)
        {
        case 1:
            m_previewMesh = geometry::Primitives::Cube(1.4f);
            break;
        case 2:
            m_previewMesh = geometry::Primitives::Plane(2.0f, 2.0f);
            break;
        case 3:
            m_previewMesh = geometry::Primitives::Cylinder(0.7f, 1.6f, 48);
            break;
        case 4:
            m_previewMesh = geometry::Primitives::Torus(0.8f, 0.35f, 48, 24);
            break;
        case 5:
            m_previewMesh = geometry::Primitives::Cone(0.8f, 1.6f, 48);
            break;
        default:
            m_previewMesh = geometry::Primitives::Sphere(1.0f, 48, 24);
            break;
        }
        mc->mesh.SetId(Guid{});
        mc->mesh = m_previewMesh.Get(); // direct override (runtime-built, not an asset)
        FramePreview(m_previewMesh.Get());
    }

    void MaterialEditorPage::FramePreview(const foundation::geometry::StaticMesh* mesh)
    {
        f32 radius = 1.0f;
        Float3 center{0.0f, 0.0f, 0.0f};
        if (mesh != nullptr && mesh->VertexCount() > 0)
        {
            center = mesh->bounds.Center();
            radius = core::Max(0.25f, Length(mesh->bounds.Extents()));
        }
        if (m_preview)
        {
            m_preview->Camera().FrameBounds(center, radius);
        }
    }

    void MaterialEditorPage::RebuildGrid()
    {
        m_grid->Clear();
        m_refreshers.Clear();
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        MaterialEditorPage* self = this;
        const materials::MaterialSource& src = m_asset->source;

        // --- Material: shader + pipeline state ---
        const StringView shaderShown =
            src.shaderName.IsEmpty() ? StringView(u8"(shader asset)") : src.shaderName.AsView();
        auto shader = MakeRef<ui::toolkit::StringEditor>(DefaultAllocator(), StringView(u8"Shader"),
                                                         shaderShown, Function<void(StringView)>{},
                                                         StringView(u8"Material"));
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(shader.Get()));

        // --- Preview: geometry the material is shown on (page-local, not saved) ---
        {
            static constexpr StringView kShapes[] = {u8"Sphere",   u8"Cube",  u8"Plane",
                                                     u8"Cylinder", u8"Torus", u8"Cone"};
            auto shape = MakeRef<ui::toolkit::EnumEditor>(
                DefaultAllocator(), StringView(u8"Shape"), static_cast<i32>(m_previewShape),
                Span<const StringView>{kShapes, 6},
                Function<void(i32)>{[self](i32 index)
                                    {
                                        self->m_previewShape =
                                            static_cast<u32>(core::Max(0, index));
                                        self->m_previewMeshGuid =
                                            Guid{}; // shape picks override an asset mesh
                                        self->ApplyPreviewMesh();
                                        self->SavePreviewPref();
                                    }},
                StringView(u8"Preview"));
            m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(shape.Get()));

            auto meshName = [self]() -> StringView
            {
                if (self->m_previewMeshGuid.IsNil())
                {
                    return u8"(primitive)";
                }
                if (self->m_context->Project() != nullptr)
                {
                    if (foundation::content::Instance* inst =
                            self->m_context->Project()->SourceDb().GetInstance(
                                self->m_previewMeshGuid))
                    {
                        return inst->Name();
                    }
                }
                return u8"(missing)";
            };
            auto meshRow = MakeRef<ResourceRefEditor>(DefaultAllocator(), StringView(u8"Mesh"),
                                                      meshName(), StringView(u8"Preview"));
            ResourceRefEditor* meshRaw = meshRow.Get();
            meshRow->OnPick = [self, meshRaw, meshName]()
            {
                if (self->m_content->Context == nullptr || self->m_context->Project() == nullptr)
                {
                    return;
                }
                Array<String> typeNames;
                typeNames.PushBack(String(u8"StaticMeshAsset"));
                typeNames.PushBack(String(u8"SkinnedMeshAsset"));
                auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_context, Move(typeNames));
                dialog->OnPicked = [self, meshRaw, meshName](const Guid& picked)
                {
                    self->m_previewMeshGuid = picked; // nil (Clear) = back to the primitive
                    self->ApplyPreviewMesh();
                    self->SavePreviewPref();
                    meshRaw->SetValueText(meshName());
                };
                dialog->Show(self->m_content->Context);
            };
            m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(meshRow.Get()));
        }

        static constexpr StringView kBlendItems[] = {u8"Opaque",     u8"Masked",
                                                     u8"AlphaBlend", u8"Additive",
                                                     u8"Multiply",   u8"PremultipliedAlpha"};
        static constexpr StringView kDepthItems[] = {u8"Disabled", u8"ReadWrite", u8"ReadOnly",
                                                     u8"WriteOnly"};
        static constexpr StringView kCullItems[] = {u8"None", u8"Back", u8"Front"};
        // These render-state fields are now typed enums (each `: u8`); this bespoke dropdown edits
        // the index in place, so it aliases the enum byte through its identical u8 representation.
        // (A future reflection-first material page would use the reflected enum directly.)
        AddPipelineEnumRow(u8"Blend", Span<const StringView>{kBlendItems, 6},
                           [](materials::MaterialSource& s) -> u8& {
                               return reinterpret_cast<u8&>(s.blendMode);
                           });
        AddPipelineEnumRow(u8"Depth", Span<const StringView>{kDepthItems, 4},
                           [](materials::MaterialSource& s) -> u8& {
                               return reinterpret_cast<u8&>(s.depthMode);
                           });
        AddPipelineEnumRow(u8"Cull", Span<const StringView>{kCullItems, 3},
                           [](materials::MaterialSource& s) -> u8& {
                               return reinterpret_cast<u8&>(s.cullMode);
                           });

        // --- Properties: the source's uniform table (Float / Float4-as-color today) ---
        for (usize i = 0; i < src.propNames.Size(); ++i)
        {
            const auto type = static_cast<materials::MaterialPropertyType>(src.propTypes[i]);
            const String name = src.propNames[i]; // copy: the grid outlives rebuilds of src arrays
            if (type == materials::MaterialPropertyType::Float)
            {
                auto value = [self, name]() -> f64
                {
                    f32 v = 0.0f;
                    self->ReadUniform(name.AsView(), &v, sizeof(v));
                    return static_cast<f64>(v);
                };
                auto write = [self, name](f32 f)
                {
                    self->ApplyEdit(name.AsView(),
                                    Function<void(materials::MaterialSource&)>{
                                        [self, name, f](materials::MaterialSource&)
                                        { self->WriteUniform(name.AsView(), &f, sizeof(f)); }});
                };
                // The standard PBR factors are all 0..1 - slider+field like the scene
                // inspector's range rows; unknown floats keep the unbounded field.
                const bool zeroToOne =
                    name.AsView() == u8"Metallic" || name.AsView() == u8"Roughness" ||
                    name.AsView() == u8"OcclusionStrength" || name.AsView() == u8"AlphaCutoff";
                const bool zeroToTwo = name.AsView() == u8"NormalScale";
                if (zeroToOne || zeroToTwo)
                {
                    auto editor = MakeRef<ui::toolkit::RangeEditor>(
                        DefaultAllocator(), name.AsView(), static_cast<f32>(value()), 0.0f,
                        zeroToTwo ? 2.0f : 1.0f, 0.01f,
                        Function<void(f32)>{[write](f32 v) { write(v); }},
                        StringView(u8"Properties"));
                    editor->SetDisplayName(PrettifyPropertyName(name.AsView()).AsView());
                    AddEditor(editor.Get(), [value, raw = editor.Get()]()
                              { raw->SetValue(static_cast<f32>(value())); });
                }
                else
                {
                    auto editor = MakeRef<ui::toolkit::FloatEditor>(
                        DefaultAllocator(), name.AsView(), value(), 0.0, 1e9, 0.01, 3,
                        Function<void(f64)>{[write](f64 v) { write(static_cast<f32>(v)); }},
                        StringView(u8"Properties"));
                    editor->SetDisplayName(PrettifyPropertyName(name.AsView()).AsView());
                    AddEditor(editor.Get(),
                              [value, raw = editor.Get()]() { raw->SetValue(value()); });
                }
            }
            else if (type == materials::MaterialPropertyType::Float4)
            {
                auto value = [self, name]() -> Color
                {
                    Float4 v{1, 1, 1, 1};
                    self->ReadUniform(name.AsView(), &v, sizeof(v));
                    return Color{v.x, v.y, v.z, v.w};
                };
                auto editor = MakeRef<ui::toolkit::ColorEditor>(
                    DefaultAllocator(), name.AsView(), value(),
                    Function<void(Color)>{[self, name](Color c)
                                          {
                                              self->ApplyEdit(
                                                  name.AsView(),
                                                  Function<void(materials::MaterialSource&)>{
                                                      [self, name, c](materials::MaterialSource&)
                                                      {
                                                          const Float4 v{c.r, c.g, c.b, c.a};
                                                          self->WriteUniform(name.AsView(), &v,
                                                                             sizeof(v));
                                                      }});
                                          }},
                    StringView(u8"Properties"));
                editor->SetDisplayName(PrettifyPropertyName(name.AsView()).AsView());
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            }
            else if (type == materials::MaterialPropertyType::Texture2D ||
                     type == materials::MaterialPropertyType::TextureCube)
            {
                AddTextureRow(name);
            }
            // Sampler/Int/Matrix rows: extend when a preset needs them.
        }
    }

    void MaterialEditorPage::AddPipelineEnumRow(StringView label, Span<const StringView> items,
                                                u8& (*field)(materials::MaterialSource&))
    {
        MaterialEditorPage* self = this;
        auto read = [self, field]() -> i32
        {
            return (self->m_asset.Get() != nullptr) ? static_cast<i32>(field(self->m_asset->source))
                                                    : 0;
        };
        const String key(label);
        auto editor = MakeRef<ui::toolkit::EnumEditor>(
            DefaultAllocator(), label, read(), items,
            Function<void(i32)>{[self, field, key](i32 index)
                                {
                                    self->ApplyEdit(key.AsView(),
                                                    Function<void(materials::MaterialSource&)>{
                                                        [field, index](materials::MaterialSource& s)
                                                        { field(s) = static_cast<u8>(index); }});
                                }},
            StringView(u8"Material"));
        AddEditor(editor.Get(), [read, raw = editor.Get()]() { raw->SetValue(read()); });
    }

    void MaterialEditorPage::AddTextureRow(const String& slot)
    {
        MaterialEditorPage* self = this;
        auto target = [self, slot]() -> Guid
        {
            if (self->m_asset.Get() == nullptr)
            {
                return Guid{};
            }
            const materials::MaterialSource& s = self->m_asset->source;
            for (usize i = 0; i < s.textureSlots.Size() && i < s.textureIds.Size(); ++i)
            {
                if (s.textureSlots[i].AsView() == slot.AsView())
                {
                    return s.textureIds[i];
                }
            }
            return Guid{};
        };
        auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), slot.AsView(),
                                                 AssetNameFor(target()), StringView(u8"Textures"));
        editor->SetDisplayName(PrettifyPropertyName(slot.AsView()).AsView());
        ResourceRefEditor* raw = editor.Get();
        raw->OnPick = [self, slot]()
        {
            if (self->Context() == nullptr || self->m_context->Project() == nullptr)
            {
                return;
            }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"TextureAsset"));
            auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                DefaultAllocator(), *self->m_context, Move(typeNames));
            dialog->OnPicked = [self, slot](const Guid& picked)
            {
                self->ApplyEdit(slot.AsView(),
                                Function<void(materials::MaterialSource&)>{
                                    [slot, picked](materials::MaterialSource& s)
                                    {
                                        for (usize i = 0; i < s.textureSlots.Size(); ++i)
                                        {
                                            if (s.textureSlots[i].AsView() != slot.AsView())
                                            {
                                                continue;
                                            }
                                            if (picked.IsNil())
                                            {
                                                s.textureSlots.RemoveAt(i);
                                                s.textureIds.RemoveAt(i);
                                            }
                                            else
                                            {
                                                s.textureIds[i] = picked;
                                            }
                                            return;
                                        }
                                        if (!picked.IsNil())
                                        {
                                            s.textureSlots.PushBack(String(slot.AsView()));
                                            s.textureIds.PushBack(picked);
                                        }
                                    }});
            };
            dialog->Show(self->Context());
        };
        AddEditor(raw, [self, target, raw]() { raw->SetValueText(self->AssetNameFor(target())); });
    }

    StringView MaterialEditorPage::AssetNameFor(const Guid& target)
    {
        if (target.IsNil())
        {
            return u8"(none)";
        }
        if (m_context->Project() != nullptr)
        {
            if (foundation::content::Instance* inst =
                    m_context->Project()->SourceDb().GetInstance(target))
            {
                return inst->Name();
            }
        }
        return u8"(missing)";
    }

    void MaterialEditorPage::AddEditor(foundation::ui::toolkit::PropertyEditor* editor,
                                       Function<void()> refresher)
    {
        m_grid->AddProperty(RefPtr<foundation::ui::toolkit::PropertyEditor>(editor));
        foundation::ui::toolkit::PropertyEditor* raw = editor;
        m_refreshers.PushBack(Function<void()>{[raw, pull = Move(refresher)]()
                                               {
                                                   if (!raw->IsEditing())
                                                   {
                                                       pull();
                                                   }
                                               }});
    }

    void MaterialEditorPage::ReadUniform(StringView name, void* out, usize bytes) const
    {
        const materials::MaterialSource& s = m_asset->source;
        for (usize i = 0; i < s.propNames.Size(); ++i)
        {
            if (s.propNames[i].AsView() != name)
            {
                continue;
            }
            const u32 offset = (i < s.propOffsets.Size()) ? s.propOffsets[i] : 0u;
            if (offset + bytes <= s.uniformDefaults.Size())
            {
                MemCopy(out, s.uniformDefaults.Data() + offset, bytes);
            }
            return;
        }
    }

    void MaterialEditorPage::WriteUniform(StringView name, const void* value, usize bytes)
    {
        materials::MaterialSource& s = m_asset->source;
        for (usize i = 0; i < s.propNames.Size(); ++i)
        {
            if (s.propNames[i].AsView() != name)
            {
                continue;
            }
            const u32 offset = (i < s.propOffsets.Size()) ? s.propOffsets[i] : 0u;
            if (offset + bytes <= s.uniformDefaults.Size())
            {
                MemCopy(s.uniformDefaults.Data() + offset, value, bytes);
            }
            return;
        }
    }

    const TypeInfo* MaterialEditorPageFactory::PrimaryType() const
    {
        return &pipeline::MaterialAsset::StaticType();
    }

    UniquePtr<EditorPage>
    MaterialEditorPageFactory::CreatePage(EditorContext& context,
                                          foundation::content::Instance& instance)
    {
        MaterialEditorPage* page =
            DefaultAllocator().New<MaterialEditorPage>(context, *m_host, *m_uiHost, instance);
        return UniquePtr<EditorPage>(page, DefaultAllocator());
    }
}
