// Draconic::ModelImporter - :file_import partition.
//
// The SOURCE-side model importer for the editor pipeline (asset-pipeline design §4/§7): a
// dropped model file fans out into REAL source instances in the content DB - textures
// (embedded-pixels TextureAssets), materials (MaterialAssets), meshes (Static/SkinnedMeshAssets),
// skeleton + clips (animation assets), and a ModelManifestAsset tying them together - so the
// pipeline owns every cook incrementally and each fanned-out asset is individually editable
// (tweak one material without touching the model). Product guid = source guid keeps the
// manifest's recorded guids valid in the cooked DB, where the runtime's ModelResource composite
// binds them.
//
// (This is the editor path; CookModel in :cook remains the direct/sample path that bakes a
// loaded model straight into a runtime DB.)

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include <initializer_list>

export module draconic.modelimporter:file_import;

import draconic.core;
import draconic.model;
import draconic.model.io;
import draconic.model.gltf;
import draconic.model.fbx;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.geometry.editor;
import draconic.materials;
import draconic.materials.resource;
import draconic.materials.editor;
import draconic.texture;
import draconic.texture.editor;
import draconic.image;
import draconic.animation;
import draconic.animation.resource;
import draconic.animation.editor;
import draconic.vfs;
import draconic.content;
import draconic.editor;
import draconic.editor.core;
import :mesh_convert;
import :anim_convert;
import :resource;
import :cook;   // IsSkinnedMesh + the conversion helpers' home

using namespace draconic::core;

export namespace draconic::modelimporter
{
    namespace content = draconic::content;
    namespace ed = draconic::editor;

    // Source asset embedding a ModelManifestSource (built at import; the cook writes it
    // through). Guids inside are source guids == product guids.
    class ModelManifestAsset final : public ed::Asset
    {
        DRACONIC_OBJECT(ModelManifestAsset, ed::Asset)
    public:
        ModelManifestSource manifest;

        void Serialize(ISerializer& ar) override
        {
            ed::Asset::Serialize(ar);   // fileName = the imported model file (re-import seed)
            manifest.Serialize(ar);
        }
    };

    class ModelManifestAssetBuilder final : public ed::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override { return &ModelManifestAsset::StaticType(); }
        [[nodiscard]] const TypeInfo* ProductType() const override { return &ModelManifestSource::StaticType(); }

        // Everything the manifest points at is a runtime REFERENCE: the products must exist,
        // but their content never re-cooks the manifest.
        void ScanDependencies(const ed::Asset& asset, ed::AssetBuildContext&,
                              ed::AssetDependencies& out) override
        {
            const ModelManifestAsset& ma = static_cast<const ModelManifestAsset&>(asset);
            for (const Guid& g : ma.manifest.meshGuids) { out.references.PushBack(g); }
            for (const Guid& g : ma.manifest.materialGuids) { out.references.PushBack(g); }
            for (const Guid& g : ma.manifest.materialAlbedo) { if (!g.IsNil()) { out.references.PushBack(g); } }
            for (const Guid& g : ma.manifest.animationGuids) { out.references.PushBack(g); }
            if (!ma.manifest.skeletonGuid.IsNil()) { out.references.PushBack(ma.manifest.skeletonGuid); }
        }

        [[nodiscard]] Status Build(const ed::Asset& asset, ed::AssetBuildContext& ctx) override
        {
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }
            const ModelManifestAsset& ma = static_cast<const ModelManifestAsset&>(asset);
            return ctx.output->WriteObject(const_cast<ModelManifestSource&>(ma.manifest));
        }
    };

    /// Options for one model import (the import dialog renders the toggles).
    class ModelImportOptions final : public ed::ImportOptions
    {
        DRACONIC_OBJECT(ModelImportOptions, ed::ImportOptions)
    public:
        bool importTextures = true;     // embedded/sidecar images -> TextureAssets
        bool importMaterials = true;    // PBR materials (texture slots wired when textures import)
        bool importAnimations = true;   // skeleton + clips
        bool generatePrefab = true;     // hierarchy prefab beside the manifest (post-import step)

        [[nodiscard]] Array<Toggle> Toggles() override
        {
            Array<Toggle> toggles;
            toggles.PushBack(Toggle{ u8"Textures", u8"Import the model's images as texture assets", &importTextures });
            toggles.PushBack(Toggle{ u8"Materials", u8"Import PBR materials (textures wire in when they import too)", &importMaterials });
            toggles.PushBack(Toggle{ u8"Animations", u8"Import the skeleton and animation clips", &importAnimations });
            toggles.PushBack(Toggle{ u8"Generate prefab", u8"Create a spawnable prefab of the model's node hierarchy; re-import regenerates it", &generatePrefab });
            return toggles;
        }

        void Serialize(ISerializer& ar) override
        {
            u8 textures = importTextures ? 1u : 0u;
            u8 materials = importMaterials ? 1u : 0u;
            u8 animations = importAnimations ? 1u : 0u;
            u8 prefab = generatePrefab ? 1u : 0u;
            draconic::core::Serialize(ar, "textures", textures);
            draconic::core::Serialize(ar, "materials", materials);
            draconic::core::Serialize(ar, "animations", animations);
            draconic::core::Serialize(ar, "prefab", prefab);
            importTextures = textures != 0;
            importMaterials = materials != 0;
            importAnimations = animations != 0;
            generatePrefab = prefab != 0;
        }
    };

    /// OS-file importer for model files: loads through draconic.model and fans out source
    /// instances into a subgroup named after the file stem.
    class ModelFileImporter final : public ed::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Model"; }

        [[nodiscard]] RefPtr<ed::ImportOptions> CreateOptions() const override
        {
            return RefPtr<ed::ImportOptions>(MakeRef<ModelImportOptions>(DefaultAllocator()).Get());
        }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            for (StringView ext : { u8"glb", u8"gltf", u8"fbx" })
            {
                if (extension == ext) { return true; }
            }
            return false;
        }

        [[nodiscard]] Result<content::Instance*> Import(StringView sourcePath,
                                                        ed::EditorProject& project,
                                                        content::Group& group,
                                                        const ed::ImportOptions* options) override
        {
            const ModelImportOptions defaults;
            const ModelImportOptions& opt = (options != nullptr)
                ? static_cast<const ModelImportOptions&>(*options) : defaults;
            Result<String> fileName = ed::CopyIntoSources(project, sourcePath);
            if (!fileName.HasValue()) { return Err(fileName.Error()); }

            // Load from the ORIGINAL dropped path: .gltf files reference sibling sidecars
            // (.bin buffers, image files) that live next to the original, not in Sources/.
            // The sidecars are copied into Sources/ below for provenance/re-import.
            const StringView loadPath = sourcePath;
            draconic::model::Model model;
            draconic::model::gltf::GltfLoader gltfLoader;
            draconic::model::fbx::FbxLoader fbxLoader;
            draconic::model::io::registerLoader(&gltfLoader);
            draconic::model::io::registerLoader(&fbxLoader);
            const draconic::model::ModelLoadResult loaded =
                draconic::model::io::loadModel(loadPath, model);
            draconic::model::io::unregisterLoader(&fbxLoader);
            draconic::model::io::unregisterLoader(&gltfLoader);
            if (loaded != draconic::model::ModelLoadResult::Ok)
            {
                DRACONIC_LOG_ERROR(u8"Import", u8"model load failed ({}): {}",
                                   static_cast<u32>(loaded), fileName.Value());
                return Err(ErrorCode::InvalidArgument);
            }
            model.calculateBounds();

            // .gltf: copy the referenced sidecars (buffers/images by relative uri) into
            // Sources/ so the imported source set is complete.
            if (ed::FileExtensionLower(sourcePath) == StringView(u8"gltf"))
            {
                CopyGltfSidecars(sourcePath, project);
            }

            const StringView stem = ed::FileStemOf(fileName.Value().AsView());
            content::Group* modelGroup = group.CreateGroup(stem);
            if (modelGroup == nullptr) { return Err(ErrorCode::Unknown); }

            ModelManifestAsset manifestAsset;
            manifestAsset.fileName = fileName.Value();
            ModelManifestSource& manifest = manifestAsset.manifest;
            manifest.boundsMin = model.bounds().min;
            manifest.boundsMax = model.bounds().max;

            Array<String> claimed;   // names claimed THIS run (ClaimInstance's dedup scope)
            Array<Guid> textureGuids;
            if (opt.importTextures) { ImportTextures(model, *modelGroup, textureGuids, claimed); }
            else { for (usize i = 0; i < model.textures().Size(); ++i) { textureGuids.PushBack(Guid{}); } }
            if (opt.importMaterials) { ImportMaterials(model, *modelGroup, textureGuids, manifest, claimed); }
            if (opt.importAnimations) { ImportSkeletonAndClips(model, *modelGroup, manifest, claimed); }
            const Status meshes = ImportMeshes(model, *modelGroup, manifest, claimed);
            if (!meshes.IsOk()) { return Err(meshes.Code()); }
            ImportNodes(model, manifest);

            content::Instance* instance =
                modelGroup->CreateInstance(stem, ModelManifestAsset::StaticType());
            if (instance == nullptr) { return Err(ErrorCode::Unknown); }
            const Status written = instance->WriteObject(manifestAsset);
            if (!written.IsOk()) { return Err(written.Code()); }
            return instance;
        }

    private:
        // Copy every relative "uri" the .gltf references (buffers, images) from next to the
        // original file into Sources/, preserving relative subpaths. Data URIs and
        // parent-escaping paths are skipped. A plain text scan (the uris live in JSON string
        // values); failures only log - the import itself already succeeded from the original.
        static void CopyGltfSidecars(StringView originalPath, ed::EditorProject& project)
        {
            Result<Array<byte>> bytes = ReadFile(originalPath);
            if (!bytes.HasValue()) { return; }
            const StringView text(reinterpret_cast<const utf8char*>(bytes.Value().Data()),
                                  bytes.Value().Size());

            // Directory of the original file.
            usize dirEnd = 0;
            for (usize i = originalPath.Size(); i > 0; --i)
            {
                const utf8char c = originalPath[i - 1];
                if (c == utf8char('/') || c == utf8char('\\')) { dirEnd = i; break; }
            }
            const StringView dir = originalPath.SubStr(0, dirEnd);

            draconic::vfs::NativeFileSystem sources(project.SourcesRoot().AsView());
            const StringView key = u8"\"uri\"";
            for (usize i = 0; i + key.Size() < text.Size(); ++i)
            {
                if (text.SubStr(i, key.Size()) != key) { continue; }
                usize j = i + key.Size();
                while (j < text.Size() && (text[j] == utf8char(':') || text[j] == utf8char(' ')
                                        || text[j] == utf8char('\t'))) { ++j; }
                if (j >= text.Size() || text[j] != utf8char('"')) { continue; }
                const usize begin = ++j;
                while (j < text.Size() && text[j] != utf8char('"')) { ++j; }
                if (j >= text.Size()) { break; }
                const StringView uri = text.SubStr(begin, j - begin);
                i = j;

                if (uri.IsEmpty()) { continue; }
                if (uri.Size() >= 5 && uri.SubStr(0, 5) == StringView(u8"data:")) { continue; }
                bool escapes = false;
                for (usize k = 0; k + 1 < uri.Size(); ++k)
                {
                    if (uri[k] == utf8char('.') && uri[k + 1] == utf8char('.')) { escapes = true; break; }
                }
                if (escapes) { continue; }

                String from(dir);
                from.Append(uri);
                Result<Array<byte>> payload = ReadFile(from.AsView());
                if (!payload.HasValue())
                {
                    DRACONIC_LOG_WARNING(u8"Import", u8"gltf sidecar missing: {}", uri);
                    continue;
                }
                const Status saved = sources.AsWritable()->Save(uri,
                    Span<const byte>(payload.Value().Data(), payload.Value().Size()));
                if (!saved.IsOk())
                {
                    DRACONIC_LOG_WARNING(u8"Import", u8"gltf sidecar copy failed: {}", uri);
                }
            }
        }

        // Reuse-or-claim (re-import semantics): a same-named instance OF THE SAME TYPE from a
        // previous import is REUSED - its guid survives, so cooked products overwrite in place
        // and everything referencing it (materials, prefabs, placed scenes) follows the
        // re-imported content. Names already claimed THIS run (two source textures named
        // alike) or squatted by a DIFFERENT type get numeric suffixes, like UniqueName did.
        [[nodiscard]] static content::Instance* ClaimInstance(content::Group& group, StringView base,
                                                              const TypeInfo& type,
                                                              Array<String>& claimed)
        {
            String name(base);
            for (u32 n = 2;; ++n)
            {
                bool taken = false;
                for (const String& c : claimed)
                {
                    if (c.AsView() == name.AsView()) { taken = true; break; }
                }
                if (!taken)
                {
                    content::Instance* existing = group.GetInstance(name.AsView());
                    const StringView typeName(reinterpret_cast<const utf8char*>(type.name));
                    if (existing == nullptr || existing->TypeName() == typeName)
                    {
                        content::Instance* instance = (existing != nullptr)
                            ? existing : group.CreateInstance(name.AsView(), type);
                        if (instance != nullptr) { claimed.PushBack(Move(name)); }
                        return instance;
                    }
                }
                name = Format(u8"{}.{}", base, n);
            }
        }

        static void ImportTextures(const draconic::model::Model& model, content::Group& group,
                                   Array<Guid>& outGuids, Array<String>& claimed)
        {
            // Color space follows USAGE: data maps (normal/MR/AO) stay linear - sRGB-decoding
            // them corrupts the values (a flat normal 0.5 would linearize to ~0.21).
            Array<bool> linear;
            ClassifyLinearTextures(model, linear);

            const Span<draconic::model::ModelTexture* const> textures = model.textures();
            for (usize i = 0; i < textures.Size(); ++i)
            {
                const draconic::model::ModelTexture& t = *textures[i];
                const u8* data = t.getData();
                const i32 size = t.getDataSize();
                const bool rgba8 = (data != nullptr && t.width > 0 && t.height > 0
                                 && size == t.width * t.height * 4);
                if (!rgba8) { outGuids.PushBack(Guid{}); continue; }

                draconic::texture::TextureAsset asset;
                asset.embeddedWidth = static_cast<u32>(t.width);
                asset.embeddedHeight = static_cast<u32>(t.height);
                asset.colorSpace = (i < linear.Size() && linear[i])
                    ? draconic::image::ImageColorSpace::Linear   // data maps (normal/MR/AO)
                    : draconic::image::ImageColorSpace::Srgb;    // color maps (albedo/emissive)
                asset.generateMipmaps = false;

                // Real names when the source has them (rules out slot mix-ups at a glance).
                content::Instance* inst = ClaimInstance(group, ImportedTextureName(t, i).AsView(),
                    draconic::texture::TextureAsset::StaticType(), claimed);
                if (inst == nullptr || !inst->WriteObject(asset).IsOk()) { outGuids.PushBack(Guid{}); continue; }
                const Status ds = inst->WriteData(u8"pixels",
                    Span<const byte>{ reinterpret_cast<const byte*>(data), static_cast<usize>(size) });
                outGuids.PushBack(ds.IsOk() ? inst->Id() : Guid{});
            }
        }

        // PBR factors -> MaterialAsset instances (builtin "forward" shader by name).
        // Bake-once cache for FBX separate metal/rough pairs (materials often share maps).
        static Guid GetOrBakePackedMR(const draconic::model::Model& model, content::Group& group,
                                      HashMap<u64, Guid>& cache, i32 roughIdx, i32 metalIdx,
                                      Array<String>& claimed)
        {
            const u64 key = (static_cast<u64>(static_cast<u32>(roughIdx)) << 32)
                          | static_cast<u64>(static_cast<u32>(metalIdx));
            if (const Guid* hit = cache.Find(key)) { return *hit; }

            u32 w = 0, h = 0;
            Array<u8> pixels = BakePackedMetallicRoughness(model, roughIdx, metalIdx, w, h);
            if (pixels.IsEmpty()) { cache.InsertOrAssign(key, Guid{}); return Guid{}; }

            draconic::texture::TextureAsset asset;
            asset.embeddedWidth = w;
            asset.embeddedHeight = h;
            asset.colorSpace = draconic::image::ImageColorSpace::Linear;   // data map
            asset.generateMipmaps = false;
            const String name = Format(u8"mr.packed.{}.{}", roughIdx, metalIdx);
            content::Instance* inst = ClaimInstance(group, name.AsView(),
                draconic::texture::TextureAsset::StaticType(), claimed);
            if (inst == nullptr || !inst->WriteObject(asset).IsOk()
                || !inst->WriteData(u8"pixels",
                       Span<const byte>{ reinterpret_cast<const byte*>(pixels.Data()), pixels.Size() }).IsOk())
            {
                cache.InsertOrAssign(key, Guid{});
                return Guid{};
            }
            cache.InsertOrAssign(key, inst->Id());
            return inst->Id();
        }

        static void ImportMaterials(const draconic::model::Model& model, content::Group& group,
                                    const Array<Guid>& textureGuids, ModelManifestSource& manifest,
                                    Array<String>& claimed)
        {
            HashMap<u64, Guid> bakedMR;   // per-pair bake cache (see GetOrBakePackedMR)
            const Span<draconic::model::ModelMaterial* const> materials = model.materials();
            for (usize i = 0; i < materials.Size(); ++i)
            {
                const draconic::model::ModelMaterial& m = *materials[i];
                RefPtr<draconic::materials::Material> built = draconic::materials::CreatePBR(
                    ImportedAssetName(m.name(), u8"mat", i).AsView(),
                    m.baseColorFactor, m.metallicFactor, m.roughnessFactor);
                built->SetDefaultColor(u8"EmissiveColor",
                    Float4{ m.emissiveFactor.x, m.emissiveFactor.y, m.emissiveFactor.z, 1.0f });
                built->SetDefaultFloat(u8"OcclusionStrength", m.occlusionStrength);
                built->SetDefaultFloat(u8"NormalScale", m.normalScale);
                built->SetDefaultFloat(u8"AlphaCutoff", m.alphaCutoff);
                draconic::materials::MaterialAsset asset;
                draconic::materials::MaterialImporter::Import(*built, Guid{}, asset);
                asset.source.shaderName = String(u8"forward");
                MaterialSamplerModes(model, m, asset.source.samplerU, asset.source.samplerV);

                // Wire EVERY authored texture INTO the material source (self-contained cooked
                // material - a directly-picked material renders fully textured, not just via
                // the model-spawn composite). Previously only the albedo made it across.
                const auto wire = [&](i32 texIdx, StringView slot) {
                    if (texIdx >= 0 && static_cast<usize>(texIdx) < textureGuids.Size()
                        && !textureGuids[static_cast<usize>(texIdx)].IsNil())
                    {
                        asset.source.textureSlots.PushBack(String(slot));
                        asset.source.textureIds.PushBack(textureGuids[static_cast<usize>(texIdx)]);
                    }
                };
                wire(m.baseColorTextureIndex,         u8"AlbedoMap");
                wire(m.normalTextureIndex,            u8"NormalMap");
                wire(m.occlusionTextureIndex,         u8"OcclusionMap");
                wire(m.emissiveTextureIndex,          u8"EmissiveMap");
                // Metallic-roughness: glTF's packed texture wires directly; FBX's separate
                // grayscale maps BAKE into a packed one (G=rough, B=metal) - feeding either
                // into the packed slot directly would bleed across channels.
                if (m.metallicRoughnessTextureIndex >= 0)
                {
                    wire(m.metallicRoughnessTextureIndex, u8"MetallicRoughnessMap");
                }
                else if (m.separateRoughnessTextureIndex >= 0 || m.separateMetalnessTextureIndex >= 0)
                {
                    const Guid packed = GetOrBakePackedMR(model, group, bakedMR,
                                                          m.separateRoughnessTextureIndex,
                                                          m.separateMetalnessTextureIndex, claimed);
                    if (!packed.IsNil())
                    {
                        asset.source.textureSlots.PushBack(String(u8"MetallicRoughnessMap"));
                        asset.source.textureIds.PushBack(packed);
                    }
                }
                // Authored pipeline state: alpha mode -> blend (Mask = alpha-tested cutout w/ holey
                // shadows; Blend = transparent pass) and double-sided -> no culling.
                if (m.alphaMode == draconic::model::AlphaMode::Mask) {
                    asset.source.blendMode = static_cast<u8>(draconic::materials::BlendMode::Masked);
                } else if (m.alphaMode == draconic::model::AlphaMode::Blend) {
                    asset.source.blendMode = static_cast<u8>(draconic::materials::BlendMode::AlphaBlend);
                }
                if (m.doubleSided) {
                    asset.source.cullMode = static_cast<u8>(draconic::materials::CullModeConfig::None);
                }

                content::Instance* inst = ClaimInstance(group,
                    ImportedAssetName(m.name(), u8"mat", i).AsView(),
                    draconic::materials::MaterialAsset::StaticType(), claimed);
                if (inst == nullptr || !inst->WriteObject(asset).IsOk())
                {
                    manifest.materialGuids.PushBack(Guid{});
                    manifest.materialAlbedo.PushBack(Guid{});
                    continue;
                }
                manifest.materialGuids.PushBack(inst->Id());

                const i32 tIdx = m.baseColorTextureIndex;
                manifest.materialAlbedo.PushBack(
                    (tIdx >= 0 && static_cast<usize>(tIdx) < textureGuids.Size())
                        ? textureGuids[static_cast<usize>(tIdx)] : Guid{});
            }
        }

        static void ImportSkeletonAndClips(const draconic::model::Model& model, content::Group& group,
                                           ModelManifestSource& manifest, Array<String>& claimed)
        {
            if (model.skins().Size() == 0) { return; }
            const draconic::model::ModelSkin& skin = *model.skins()[0];
            const HashMap<i32, i32> boneToJoint = BuildBoneToJoint(skin);

            draconic::animation::SkeletonAsset skeleton;
            SkeletonSourceFromModel(model, skin, boneToJoint, skeleton.source);
            content::Instance* skelInst = ClaimInstance(group,
                skin.name().IsEmpty() ? StringView(u8"skeleton")
                                      : ImportedAssetName(skin.name(), u8"skeleton", 0).AsView(),
                draconic::animation::SkeletonAsset::StaticType(), claimed);
            if (skelInst != nullptr && skelInst->WriteObject(skeleton).IsOk())
            {
                manifest.skeletonGuid = skelInst->Id();
            }

            const Span<draconic::model::ModelAnimation* const> animations = model.animations();
            for (usize a = 0; a < animations.Size(); ++a)
            {
                content::Instance* clipInst = ClaimInstance(group,
                    ImportedAssetName(animations[a]->name(), u8"anim", a).AsView(),
                    draconic::animation::AnimationClipAsset::StaticType(), claimed);
                draconic::animation::AnimationClipAsset clip;
                AnimationClipSourceFromModel(*animations[a], boneToJoint,
                    (clipInst != nullptr) ? clipInst->Name() : StringView(u8"anim"), clip.source);
                if (clipInst != nullptr && clipInst->WriteObject(clip).IsOk())
                {
                    manifest.animationGuids.PushBack(clipInst->Id());
                }
            }
        }

        [[nodiscard]] static Status ImportMeshes(const draconic::model::Model& model,
                                                 content::Group& group, ModelManifestSource& manifest,
                                                 Array<String>& claimed)
        {
            const bool hasSkin = model.skins().Size() > 0;
            const Span<draconic::model::ModelMesh* const> meshes = model.meshes();
            for (usize i = 0; i < meshes.Size(); ++i)
            {
                const draconic::model::ModelMesh& m = *meshes[i];
                const bool skinned = IsSkinnedMesh(m) && hasSkin;
                const String baseName = ImportedAssetName(m.name(), u8"mesh", i);
                const StringView name = baseName.AsView();

                content::Instance* inst = nullptr;
                Status written;
                if (skinned)
                {
                    draconic::geometry::SkinnedMeshAsset asset;
                    SkinnedMeshSourceFromModel(m, 0, asset.source);
                    inst = ClaimInstance(group, name, draconic::geometry::SkinnedMeshAsset::StaticType(), claimed);
                    if (inst == nullptr) { return Status{ ErrorCode::Unknown }; }
                    written = inst->WriteObject(asset);
                }
                else
                {
                    draconic::geometry::StaticMeshAsset asset;
                    StaticMeshSourceFromModel(m, asset.source);
                    inst = ClaimInstance(group, name, draconic::geometry::StaticMeshAsset::StaticType(), claimed);
                    if (inst == nullptr) { return Status{ ErrorCode::Unknown }; }
                    written = inst->WriteObject(asset);
                }
                if (!written.IsOk()) { return written; }

                manifest.meshGuids.PushBack(inst->Id());
                manifest.meshSkinned.PushBack(skinned ? u8{ 1 } : u8{ 0 });
                const Span<const draconic::model::ModelMeshPart> parts = m.parts();
                manifest.meshMaterial.PushBack(parts.Size() > 0 ? parts[0].materialIndex : -1);
            }
            return Status{};
        }

        static void ImportNodes(const draconic::model::Model& model, ModelManifestSource& manifest)
        {
            const Span<draconic::model::ModelBone* const> bones = model.bones();
            for (usize i = 0; i < bones.Size(); ++i)
            {
                const draconic::model::ModelBone& b = *bones[i];
                ModelNode n;
                n.name = String(b.name());
                n.parentIndex = b.parentIndex;
                n.localTransform.position = b.translation;
                n.localTransform.rotation = b.rotation;
                n.localTransform.scale = b.scale;
                n.meshIndex = b.meshIndex;
                manifest.nodes.PushBack(Move(n));
            }
        }
    };

    // Registers the manifest asset type for content-DB construction + deserialization.
    inline void RegisterModelManifestAsset()
    {
        GlobalTypeRegistry().Register(ModelManifestAsset::StaticType());
        RegisterSerializable<ModelManifestAsset>();
    }

    DRACONIC_DEFINE_OBJECT(ModelManifestAsset, "draconic::modelimporter")
    DRACONIC_DEFINE_OBJECT(ModelImportOptions, "draconic::modelimporter")
}
