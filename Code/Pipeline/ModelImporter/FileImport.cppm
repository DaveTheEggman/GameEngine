// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::ModelImporter - :file_import partition.
//
// The SOURCE-side model importer for the editor pipeline: a
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

export module modelimporter:file_import;

import foundation.core;
import foundation.model;
import foundation.model.io;
import foundation.model.gltf;
import foundation.model.fbx;
import foundation.geometry;
import foundation.geometry.resource;
import geometry.pipeline;
import foundation.materials;
import foundation.materials.resource;
import materials.pipeline;
import foundation.texture;
import texture.pipeline;
import foundation.image;
import foundation.animation;
import foundation.animation.resource;
import animation.pipeline;
import foundation.vfs;
import foundation.content;
import pipeline.core;
import pipeline.importer;
import physics.pipeline;
import :mesh_convert;
import :anim_convert;
import foundation.model.resource;
import :cook; // IsSkinnedMesh + the conversion helpers' home

using namespace foundation::core;

export namespace pipeline
{
    // The cooked-model runtime types now live in foundation::model (foundation.model.resource).
    using foundation::model::ModelManifestSource;
    using foundation::model::ModelNode;
    using foundation::model::ModelResource;

    namespace content = foundation::content;

    // Source asset embedding a ModelManifestSource (built at import; the cook writes it
    // through). Guids inside are source guids == product guids.
    class ModelManifestAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(ModelManifestAsset, pipeline::Asset)
    public:
        ModelManifestSource manifest;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName = the imported model file (re-import seed)
            manifest.Serialize(ar);
        }
    };

    class ModelManifestAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &ModelManifestAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &ModelManifestSource::StaticType();
        }

        // Everything the manifest points at is a runtime REFERENCE: the products must exist,
        // but their content never re-cooks the manifest.
        void ScanDependencies(const pipeline::Asset& asset, pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const ModelManifestAsset& ma = static_cast<const ModelManifestAsset&>(asset);
            for (const Guid& g : ma.manifest.meshGuids)
            {
                out.references.PushBack(g);
            }
            for (const Guid& g : ma.manifest.materialGuids)
            {
                out.references.PushBack(g);
            }
            for (const Guid& g : ma.manifest.materialAlbedo)
            {
                if (!g.IsNil())
                {
                    out.references.PushBack(g);
                }
            }
            for (const Guid& g : ma.manifest.animationGuids)
            {
                out.references.PushBack(g);
            }
            if (!ma.manifest.skeletonGuid.IsNil())
            {
                out.references.PushBack(ma.manifest.skeletonGuid);
            }
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            const ModelManifestAsset& ma = static_cast<const ModelManifestAsset&>(asset);
            return ctx.output->WriteObject(const_cast<ModelManifestSource&>(ma.manifest));
        }
    };

    /// Options for one model import (the import dialog renders the toggles).
    class ModelImportOptions final : public pipeline::ImportOptions
    {
        RTTI_OBJECT(ModelImportOptions, pipeline::ImportOptions)
    public:
        bool importTextures = true;     // embedded/sidecar images -> TextureAssets
        bool importMaterials = true;    // PBR materials (texture slots wired when textures import)
        bool importAnimations = true;   // skeleton + clips
        bool generatePrefab = true;     // hierarchy prefab beside the manifest (post-import step)
        bool generateScene = false;     // standalone scene of the same hierarchy (post-import step)
        bool generateCollision = false; // CollisionShapeAsset per mesh + colliders on the prefab
        bool collisionConvex = false;   // hull (dynamic-capable) instead of exact triangle mesh
        bool generateLods = true;       // auto-LOD chains for big static meshes (authored _LODn wins)

        [[nodiscard]] Array<Toggle> Toggles() override
        {
            Array<Toggle> toggles;
            toggles.PushBack(Toggle{u8"Textures", u8"Import the model's images as texture assets",
                                    &importTextures});
            toggles.PushBack(Toggle{
                u8"Materials", u8"Import PBR materials (textures wire in when they import too)",
                &importMaterials});
            toggles.PushBack(Toggle{u8"Animations", u8"Import the skeleton and animation clips",
                                    &importAnimations});
            toggles.PushBack(Toggle{u8"Generate prefab",
                                    u8"Create a spawnable prefab of the model's node hierarchy; "
                                    u8"re-import regenerates it",
                                    &generatePrefab});
            toggles.PushBack(Toggle{u8"Generate scene",
                                    u8"Create a standalone scene of the model's node hierarchy; "
                                    u8"re-import regenerates it",
                                    &generateScene});
            toggles.PushBack(Toggle{u8"Generate collision",
                                    u8"Cook a collision shape per mesh and add colliders (+ a "
                                    u8"static rigid body) to the generated prefab",
                                    &generateCollision});
            toggles.PushBack(Toggle{
                u8"Generate LODs",
                u8"Simplified LOD chains for large static meshes (10k+ triangles); meshes with "
                u8"authored _LOD1/_LOD2 levels keep those instead",
                &generateLods});
            toggles.PushBack(Toggle{
                u8"Convex collision",
                u8"Simplified convex hulls (dynamic-capable) instead of exact triangle meshes",
                &collisionConvex});
            return toggles;
        }

        void Serialize(ISerializer& ar) override
        {
            u8 textures = importTextures ? 1u : 0u;
            u8 materials = importMaterials ? 1u : 0u;
            u8 animations = importAnimations ? 1u : 0u;
            u8 prefab = generatePrefab ? 1u : 0u;
            u8 sceneOut = generateScene ? 1u : 0u;
            u8 collision = generateCollision ? 1u : 0u;
            u8 convex = collisionConvex ? 1u : 0u;
            u8 lods = generateLods ? 1u : 0u;
            foundation::core::Serialize(ar, "textures", textures);
            foundation::core::Serialize(ar, "materials", materials);
            foundation::core::Serialize(ar, "animations", animations);
            foundation::core::Serialize(ar, "prefab", prefab);
            foundation::core::Serialize(ar, "collision", collision);
            foundation::core::Serialize(ar, "collisionConvex", convex);
            foundation::core::Serialize(ar, "scene", sceneOut);
            foundation::core::Serialize(ar, "generateLods", lods);
            importTextures = textures != 0;
            importMaterials = materials != 0;
            importAnimations = animations != 0;
            generatePrefab = prefab != 0;
            generateScene = sceneOut != 0;
            generateCollision = collision != 0;
            collisionConvex = convex != 0;
            generateLods = lods != 0;
        }
    };

    /// PrepareOnWorker's payload: the fully loaded model (parse + texture decode = the slow
    /// 95% of a model import, safely off the UI thread).
    class LoadedModel final : public Object
    {
        RTTI_OBJECT(LoadedModel, Object)
    public:
        foundation::model::Model model;
    };

    /// OS-file importer for model files: loads through foundation.model and fans out source
    /// instances into a subgroup named after the file stem.
    class ModelFileImporter final : public pipeline::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Model"; }

        [[nodiscard]] RefPtr<pipeline::ImportOptions> CreateOptions() const override
        {
            return RefPtr<pipeline::ImportOptions>(
                MakeRef<ModelImportOptions>(DefaultAllocator()).Get());
        }

        [[nodiscard]] bool WantsWorkerPrepare() const override { return true; }

        [[nodiscard]] RefPtr<Object> PrepareOnWorker(StringView sourcePath) override
        {
            RefPtr<LoadedModel> loaded = MakeRef<LoadedModel>(DefaultAllocator());
            if (LoadModelFrom(sourcePath, loaded->model) != foundation::model::ModelLoadResult::Ok)
            {
                return {};
            }
            return RefPtr<Object>(loaded.Get());
        }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            for (StringView ext : {u8"glb", u8"gltf", u8"fbx", u8"obj"})
            {
                if (extension == ext)
                {
                    return true;
                }
            }
            return false;
        }

        /// Everything Import would create, in fan-out order - the review dialog's data.
        /// Reuses the worker-prepared model when supplied; only mirrors the naming logic of
        /// the fan-out helpers (ComputeLodFold is shared so the mesh set matches exactly).
        [[nodiscard]] pipeline::ImportPlan DescribeImport(StringView sourcePath,
                                                          const pipeline::ImportOptions* options,
                                                          Object* prepared) override
        {
            const ModelImportOptions defaults;
            const ModelImportOptions& opt =
                (options != nullptr) ? static_cast<const ModelImportOptions&>(*options) : defaults;
            foundation::model::Model inlineModel;
            foundation::model::Model* modelPtr = nullptr;
            if (auto* loadedPayload = Cast<LoadedModel>(prepared))
            {
                modelPtr = &loadedPayload->model;
            }
            else
            {
                if (LoadModelFrom(sourcePath, inlineModel) !=
                    foundation::model::ModelLoadResult::Ok)
                {
                    return {};
                }
                modelPtr = &inlineModel;
            }
            const foundation::model::Model& model = *modelPtr;

            pipeline::ImportPlan plan;
            const auto add = [&plan](pipeline::ImportResourceKind kind, String name)
            {
                pipeline::ImportPlanEntry e;
                e.kind = kind;
                e.targetName = name;
                e.sourceName = Move(name);
                e.enabled = true;
                plan.entries.PushBack(Move(e));
            };

            if (opt.importTextures)
            {
                const Span<foundation::model::ModelTexture* const> textures = model.textures();
                for (usize i = 0; i < textures.Size(); ++i)
                {
                    const foundation::model::ModelTexture& t = *textures[i];
                    const bool rgba8 =
                        (t.getData() != nullptr && t.width > 0 && t.height > 0 &&
                         t.getDataSize() == t.width * t.height * 4);
                    if (rgba8) // undecodable textures never become assets - keep them out
                    {
                        add(pipeline::ImportResourceKind::Texture, ImportedTextureName(t, i));
                    }
                }
            }
            if (opt.importMaterials)
            {
                const Span<foundation::model::ModelMaterial* const> materials = model.materials();
                for (usize i = 0; i < materials.Size(); ++i)
                {
                    add(pipeline::ImportResourceKind::Material,
                        ImportedAssetName(materials[i]->name(), u8"mat", i));
                }
            }
            if (opt.importAnimations && model.skins().Size() > 0)
            {
                add(pipeline::ImportResourceKind::Skeleton, SkeletonBaseName(*model.skins()[0]));
                const Span<foundation::model::ModelAnimation* const> animations =
                    model.animations();
                for (usize a = 0; a < animations.Size(); ++a)
                {
                    add(pipeline::ImportResourceKind::AnimationClip,
                        ImportedAssetName(animations[a]->name(), u8"anim", a));
                }
            }
            const bool hasSkin = model.skins().Size() > 0;
            const Span<foundation::model::ModelMesh* const> meshes = model.meshes();
            Array<i32> lodOf;
            Array<Array<usize>> lodLevels;
            ComputeLodFold(model, /*logMismatch*/ false, lodOf, lodLevels);
            for (usize i = 0; i < meshes.Size(); ++i)
            {
                if (lodOf[i] >= 0)
                {
                    continue; // folds into its base's LOD chain - not an asset of its own
                }
                add(pipeline::ImportResourceKind::Mesh,
                    ImportedAssetName(meshes[i]->name(), u8"mesh", i));
                if (opt.generateCollision && !(IsSkinnedMesh(*meshes[i]) && hasSkin))
                {
                    String key = ImportedAssetName(meshes[i]->name(), u8"mesh", i);
                    key.Append(u8".collision");
                    add(pipeline::ImportResourceKind::Collision, Move(key));
                }
            }
            return plan;
        }

        [[nodiscard]] Result<content::Instance*>
        Import(StringView sourcePath, const pipeline::ImportContext& context, content::Group& group,
               const pipeline::ImportOptions* options, Object* prepared,
               Array<pipeline::DeferredImportWrite>* deferredWrites) override
        {
            const ModelImportOptions defaults;
            const ModelImportOptions& opt =
                (options != nullptr) ? static_cast<const ModelImportOptions&>(*options) : defaults;
            // Source provenance copy: the file NAME is known without copying; the copy
            // itself (and the .gltf sidecars below) is bulk file IO - deferred when possible.
            const StringView sourceFileName = pipeline::FileNameOf(sourcePath);
            if (sourceFileName.IsEmpty())
            {
                return Err(ErrorCode::InvalidArgument);
            }
            Result<String> fileName = Result<String>(String(sourceFileName));
            if (deferredWrites != nullptr)
            {
                pipeline::DeferredImportWrite copy;
                copy.copyFrom = String(sourcePath);
                copy.copyTo = PathJoin(context.sourcesRoot.AsView(), sourceFileName);
                deferredWrites->PushBack(static_cast<pipeline::DeferredImportWrite&&>(copy));
            }
            else
            {
                fileName = pipeline::CopyIntoSources(context, sourcePath);
                if (!fileName.HasValue())
                {
                    return Err(fileName.Error());
                }
            }

            // The slow load either arrived pre-baked from the worker phase, or runs inline
            // (headless/tests). Loading uses the ORIGINAL dropped path: .gltf files
            // reference sibling sidecars living next to the original, not in Sources/.
            foundation::model::Model inlineModel;
            foundation::model::Model* modelPtr = nullptr;
            if (auto* loadedPayload = Cast<LoadedModel>(prepared))
            {
                modelPtr = &loadedPayload->model;
            }
            else
            {
                if (LoadModelFrom(sourcePath, inlineModel) != foundation::model::ModelLoadResult::Ok)
                {
                    LOG_ERROR(u8"Import", u8"model load failed: {}", fileName.Value());
                    return Err(ErrorCode::InvalidArgument);
                }
                modelPtr = &inlineModel;
            }
            foundation::model::Model& model = *modelPtr;

            // .gltf: copy the referenced sidecars (buffers/images by relative uri) into
            // Sources/ so the imported source set is complete.
            if (pipeline::FileExtensionLower(sourcePath) == StringView(u8"gltf"))
            {
                CopyGltfSidecars(sourcePath, context, deferredWrites);
            }

            const StringView stem = pipeline::FileStemOf(fileName.Value().AsView());
            content::Group* modelGroup = group.CreateGroup(stem);
            if (modelGroup == nullptr)
            {
                return Err(ErrorCode::Unknown);
            }

            ModelManifestAsset manifestAsset;
            manifestAsset.fileName = foundation::vfs::SourcePath(fileName.Value().AsView());
            ModelManifestSource& manifest = manifestAsset.manifest;
            manifest.boundsMin = model.bounds().min;
            manifest.boundsMax = model.bounds().max;

            Array<String> claimed; // names claimed THIS run (ClaimInstance's dedup scope)
            Array<Guid> textureGuids;
            if (opt.importTextures)
            {
                ImportTextures(model, *modelGroup, textureGuids, claimed, deferredWrites, opt);
            }
            else
            {
                for (usize i = 0; i < model.textures().Size(); ++i)
                {
                    textureGuids.PushBack(Guid{});
                }
            }
            if (opt.importMaterials)
            {
                ImportMaterials(model, *modelGroup, textureGuids, manifest, claimed,
                                deferredWrites, opt);
            }
            if (opt.importAnimations)
            {
                ImportSkeletonAndClips(model, *modelGroup, manifest, claimed, opt);
            }
            Array<String> meshSourceNames; // per manifest mesh slot (collision's plan keys)
            const Status meshes =
                ImportMeshes(model, *modelGroup, manifest, claimed, deferredWrites,
                             opt.generateLods, opt, meshSourceNames);
            if (!meshes.IsOk())
            {
                return Err(meshes.Code());
            }
            if (opt.generateCollision)
            {
                ImportCollisionShapes(*modelGroup, manifest, opt.collisionConvex, claimed, opt,
                                      meshSourceNames);
            }
            ImportNodes(model, manifest);

            content::Instance* instance =
                modelGroup->CreateInstance(stem, ModelManifestAsset::StaticType());
            if (instance == nullptr)
            {
                return Err(ErrorCode::Unknown);
            }
            const Status written = instance->WriteObject(manifestAsset);
            if (!written.IsOk())
            {
                return Err(written.Code());
            }
            return instance;
        }

    private:
        [[nodiscard]] static foundation::model::ModelLoadResult
        LoadModelFrom(StringView sourcePath, foundation::model::Model& model)
        {
            foundation::model::gltf::GltfLoader gltfLoader;
            foundation::model::fbx::FbxLoader fbxLoader;
            foundation::model::io::registerLoader(&gltfLoader);
            foundation::model::io::registerLoader(&fbxLoader);
            const foundation::model::ModelLoadResult loaded =
                foundation::model::io::loadModel(sourcePath, model);
            foundation::model::io::unregisterLoader(&fbxLoader);
            foundation::model::io::unregisterLoader(&gltfLoader);
            if (loaded == foundation::model::ModelLoadResult::Ok)
            {
                model.calculateBounds();
            }
            return loaded;
        }

        // Copy every relative "uri" the .gltf references (buffers, images) from next to the
        // original file into Sources/, preserving relative subpaths. Data URIs and
        // parent-escaping paths are skipped. A plain text scan (the uris live in JSON string
        // values); failures only log - the import itself already succeeded from the original.
        static void CopyGltfSidecars(StringView originalPath, const pipeline::ImportContext& context,
                                     Array<pipeline::DeferredImportWrite>* deferredWrites)
        {
            Result<Array<byte>> bytes = ReadFile(originalPath);
            if (!bytes.HasValue())
            {
                return;
            }
            const StringView text(reinterpret_cast<const utf8char*>(bytes.Value().Data()),
                                  bytes.Value().Size());

            // Directory of the original file.
            usize dirEnd = 0;
            for (usize i = originalPath.Size(); i > 0; --i)
            {
                const utf8char c = originalPath[i - 1];
                if (c == utf8char('/') || c == utf8char('\\'))
                {
                    dirEnd = i;
                    break;
                }
            }
            const StringView dir = originalPath.SubStr(0, dirEnd);

            foundation::vfs::NativeFileSystem sources(context.sourcesRoot.AsView());
            const StringView key = u8"\"uri\"";
            for (usize i = 0; i + key.Size() < text.Size(); ++i)
            {
                if (text.SubStr(i, key.Size()) != key)
                {
                    continue;
                }
                usize j = i + key.Size();
                while (j < text.Size() && (text[j] == utf8char(':') || text[j] == utf8char(' ') ||
                                           text[j] == utf8char('\t')))
                {
                    ++j;
                }
                if (j >= text.Size() || text[j] != utf8char('"'))
                {
                    continue;
                }
                const usize begin = ++j;
                while (j < text.Size() && text[j] != utf8char('"'))
                {
                    ++j;
                }
                if (j >= text.Size())
                {
                    break;
                }
                const StringView uri = text.SubStr(begin, j - begin);
                i = j;

                if (uri.IsEmpty())
                {
                    continue;
                }
                if (uri.Size() >= 5 && uri.SubStr(0, 5) == StringView(u8"data:"))
                {
                    continue;
                }
                bool escapes = false;
                for (usize k = 0; k + 1 < uri.Size(); ++k)
                {
                    if (uri[k] == utf8char('.') && uri[k + 1] == utf8char('.'))
                    {
                        escapes = true;
                        break;
                    }
                }
                if (escapes)
                {
                    continue;
                }

                String from(dir);
                from.Append(uri);
                if (deferredWrites != nullptr)
                {
                    pipeline::DeferredImportWrite copy;
                    copy.copyFrom = from;
                    copy.copyTo = PathJoin(context.sourcesRoot.AsView(), uri);
                    deferredWrites->PushBack(static_cast<pipeline::DeferredImportWrite&&>(copy));
                    continue;
                }
                Result<Array<byte>> payload = ReadFile(from.AsView());
                if (!payload.HasValue())
                {
                    LOG_WARNING(u8"Import", u8"gltf sidecar missing: {}", uri);
                    continue;
                }
                const Status saved = sources.AsWritable()->Save(
                    uri, Span<const byte>(payload.Value().Data(), payload.Value().Size()));
                if (!saved.IsOk())
                {
                    LOG_WARNING(u8"Import", u8"gltf sidecar copy failed: {}", uri);
                }
            }
        }

        // Reuse-or-claim (re-import semantics): a same-named instance OF THE SAME TYPE from a
        // previous import is REUSED - its guid survives, so cooked products overwrite in place
        // and everything referencing it (materials, prefabs, placed scenes) follows the
        // re-imported content. Names already claimed THIS run (two source textures named
        // alike) or squatted by a DIFFERENT type get numeric suffixes, like UniqueName did.
        [[nodiscard]] static content::Instance* ClaimInstance(content::Group& group,
                                                              StringView base, const TypeInfo& type,
                                                              Array<String>& claimed)
        {
            String name(base);
            for (u32 n = 2;; ++n)
            {
                bool taken = false;
                for (const String& c : claimed)
                {
                    if (c.AsView() == name.AsView())
                    {
                        taken = true;
                        break;
                    }
                }
                if (!taken)
                {
                    content::Instance* existing = group.GetInstance(name.AsView());
                    const StringView typeName(reinterpret_cast<const utf8char*>(type.name));
                    if (existing == nullptr || existing->TypeName() == typeName)
                    {
                        content::Instance* instance =
                            (existing != nullptr) ? existing
                                                  : group.CreateInstance(name.AsView(), type);
                        if (instance != nullptr)
                        {
                            claimed.PushBack(Move(name));
                        }
                        return instance;
                    }
                }
                name = Format(u8"{}.{}", base, n);
            }
        }

        static void ImportTextures(const foundation::model::Model& model, content::Group& group,
                                   Array<Guid>& outGuids, Array<String>& claimed,
                                   Array<pipeline::DeferredImportWrite>* deferredWrites,
                                   const pipeline::ImportOptions& sel)
        {
            // Color space follows USAGE: data maps (normal/MR/AO) stay linear - sRGB-decoding
            // them corrupts the values (a flat normal 0.5 would linearize to ~0.21).
            Array<bool> linear;
            ClassifyLinearTextures(model, linear);

            const Span<foundation::model::ModelTexture* const> textures = model.textures();
            for (usize i = 0; i < textures.Size(); ++i)
            {
                const foundation::model::ModelTexture& t = *textures[i];
                const u8* data = t.getData();
                const i32 size = t.getDataSize();
                const bool rgba8 = (data != nullptr && t.width > 0 && t.height > 0 &&
                                    size == t.width * t.height * 4);
                if (!rgba8)
                {
                    outGuids.PushBack(Guid{});
                    continue;
                }

                pipeline::TextureAsset asset;
                asset.embeddedWidth = static_cast<u32>(t.width);
                asset.embeddedHeight = static_cast<u32>(t.height);
                asset.colorSpace =
                    (i < linear.Size() && linear[i])
                        ? foundation::image::ImageColorSpace::Linear // data maps (normal/MR/AO)
                        : foundation::image::ImageColorSpace::Srgb;  // color maps (albedo/emissive)
                asset.generateMipmaps = true; // mips at cook (2026-08-12) - shimmer was the no-mips gap

                // Real names when the source has them (rules out slot mix-ups at a glance).
                const String texBase = ImportedTextureName(t, i);
                if (!sel.SelectionEnabled(pipeline::ImportResourceKind::Texture, texBase.AsView()))
                {
                    outGuids.PushBack(Guid{}); // deselected: dependents wire no texture
                    continue;
                }
                content::Instance* inst = ClaimInstance(
                    group,
                    sel.SelectionName(pipeline::ImportResourceKind::Texture, texBase.AsView()),
                    pipeline::TextureAsset::StaticType(), claimed);
                if (inst == nullptr || !inst->WriteObject(asset).IsOk())
                {
                    outGuids.PushBack(Guid{});
                    continue;
                }
                const Span<const byte> pixels{reinterpret_cast<const byte*>(data),
                                              static_cast<usize>(size)};
                if (deferredWrites != nullptr)
                {
                    // Decoded pixels are the import's bulk (100s of MB for a big model) -
                    // park them for the worker flush; the view borrows from the prepared
                    // model, which the caller keeps alive until the flush completes.
                    pipeline::DeferredImportWrite write;
                    write.instance = inst;
                    write.streamName = String(u8"pixels");
                    write.view = pixels;
                    deferredWrites->PushBack(static_cast<pipeline::DeferredImportWrite&&>(write));
                    outGuids.PushBack(inst->Id());
                    continue;
                }
                const Status ds = inst->WriteData(u8"pixels", pixels);
                outGuids.PushBack(ds.IsOk() ? inst->Id() : Guid{});
            }
        }

        // PBR factors -> MaterialAsset instances (builtin "forward" shader by name).
        // Bake-once cache for FBX separate metal/rough pairs (materials often share maps).
        static Guid GetOrBakePackedMR(const foundation::model::Model& model, content::Group& group,
                                      HashMap<u64, Guid>& cache, i32 roughIdx, i32 metalIdx,
                                      Array<String>& claimed,
                                      Array<pipeline::DeferredImportWrite>* deferredWrites)
        {
            const u64 key = (static_cast<u64>(static_cast<u32>(roughIdx)) << 32) |
                            static_cast<u64>(static_cast<u32>(metalIdx));
            if (const Guid* hit = cache.Find(key))
            {
                return *hit;
            }

            u32 w = 0, h = 0;
            Array<u8> pixels = BakePackedMetallicRoughness(model, roughIdx, metalIdx, w, h);
            if (pixels.IsEmpty())
            {
                cache.InsertOrAssign(key, Guid{});
                return Guid{};
            }

            pipeline::TextureAsset asset;
            asset.embeddedWidth = w;
            asset.embeddedHeight = h;
            asset.colorSpace = foundation::image::ImageColorSpace::Linear; // data map
            asset.generateMipmaps = true; // mips at cook (2026-08-12) - shimmer was the no-mips gap
            const String name = Format(u8"mr.packed.{}.{}", roughIdx, metalIdx);
            content::Instance* inst = ClaimInstance(
                group, name.AsView(), pipeline::TextureAsset::StaticType(), claimed);
            if (inst == nullptr || !inst->WriteObject(asset).IsOk())
            {
                cache.InsertOrAssign(key, Guid{});
                return Guid{};
            }
            if (deferredWrites != nullptr)
            {
                // Baked pixels are produced HERE, so the deferred write owns them.
                pipeline::DeferredImportWrite write;
                write.instance = inst;
                write.streamName = String(u8"pixels");
                for (u8 b : pixels)
                {
                    write.owned.PushBack(static_cast<byte>(b));
                }
                deferredWrites->PushBack(static_cast<pipeline::DeferredImportWrite&&>(write));
            }
            else if (!inst->WriteData(u8"pixels",
                                      Span<const byte>{reinterpret_cast<const byte*>(pixels.Data()),
                                                       pixels.Size()})
                          .IsOk())
            {
                cache.InsertOrAssign(key, Guid{});
                return Guid{};
            }
            cache.InsertOrAssign(key, inst->Id());
            return inst->Id();
        }

        static void ImportMaterials(const foundation::model::Model& model, content::Group& group,
                                    const Array<Guid>& textureGuids, ModelManifestSource& manifest,
                                    Array<String>& claimed,
                                    Array<pipeline::DeferredImportWrite>* deferredWrites,
                                    const pipeline::ImportOptions& sel)
        {
            HashMap<u64, Guid> bakedMR; // per-pair bake cache (see GetOrBakePackedMR)
            const Span<foundation::model::ModelMaterial* const> materials = model.materials();
            for (usize i = 0; i < materials.Size(); ++i)
            {
                const foundation::model::ModelMaterial& m = *materials[i];
                const String matBase = ImportedAssetName(m.name(), u8"mat", i);
                if (!sel.SelectionEnabled(pipeline::ImportResourceKind::Material,
                                          matBase.AsView()))
                {
                    // Deselected: hold the manifest slots (nil) so submesh materialIndex
                    // stays aligned.
                    manifest.materialGuids.PushBack(Guid{});
                    manifest.materialAlbedo.PushBack(Guid{});
                    continue;
                }
                RefPtr<foundation::materials::Material> built = foundation::materials::CreatePBR(
                    ImportedAssetName(m.name(), u8"mat", i).AsView(), m.baseColorFactor,
                    m.metallicFactor, m.roughnessFactor);
                built->SetDefaultColor(
                    u8"EmissiveColor",
                    Float4{m.emissiveFactor.x, m.emissiveFactor.y, m.emissiveFactor.z, 1.0f});
                built->SetDefaultFloat(u8"OcclusionStrength", m.occlusionStrength);
                built->SetDefaultFloat(u8"NormalScale", m.normalScale);
                built->SetDefaultFloat(u8"AlphaCutoff", m.alphaCutoff);
                pipeline::MaterialAsset asset;
                pipeline::MaterialImporter::Import(*built, Guid{}, asset);
                asset.source.shaderName = String(u8"forward");
                MaterialSamplerModes(model, m, asset.source.samplerU, asset.source.samplerV);

                // Wire EVERY authored texture INTO the material source (self-contained cooked
                // material - a directly-picked material renders fully textured, not just via
                // the model-spawn composite). Previously only the albedo made it across.
                const auto wire = [&](i32 texIdx, StringView slot)
                {
                    if (texIdx >= 0 && static_cast<usize>(texIdx) < textureGuids.Size() &&
                        !textureGuids[static_cast<usize>(texIdx)].IsNil())
                    {
                        asset.source.textureSlots.PushBack(String(slot));
                        asset.source.textureIds.PushBack(textureGuids[static_cast<usize>(texIdx)]);
                    }
                };
                wire(m.baseColorTextureIndex, u8"AlbedoMap");
                wire(m.normalTextureIndex, u8"NormalMap");
                wire(m.occlusionTextureIndex, u8"OcclusionMap");
                wire(m.emissiveTextureIndex, u8"EmissiveMap");
                // Metallic-roughness: glTF's packed texture wires directly; FBX's separate
                // grayscale maps BAKE into a packed one (G=rough, B=metal) - feeding either
                // into the packed slot directly would bleed across channels.
                if (m.metallicRoughnessTextureIndex >= 0)
                {
                    wire(m.metallicRoughnessTextureIndex, u8"MetallicRoughnessMap");
                }
                else if (m.separateRoughnessTextureIndex >= 0 ||
                         m.separateMetalnessTextureIndex >= 0)
                {
                    const Guid packed =
                        GetOrBakePackedMR(model, group, bakedMR, m.separateRoughnessTextureIndex,
                                          m.separateMetalnessTextureIndex, claimed, deferredWrites);
                    if (!packed.IsNil())
                    {
                        asset.source.textureSlots.PushBack(String(u8"MetallicRoughnessMap"));
                        asset.source.textureIds.PushBack(packed);
                    }
                }
                // Authored pipeline state: alpha mode -> blend (Mask = alpha-tested cutout w/ holey
                // shadows; Blend = transparent pass) and double-sided -> no culling.
                if (m.alphaMode == foundation::model::AlphaMode::Mask)
                {
                    asset.source.blendMode =
                        foundation::materials::BlendMode::Masked;
                }
                else if (m.alphaMode == foundation::model::AlphaMode::Blend)
                {
                    asset.source.blendMode =
                        foundation::materials::BlendMode::AlphaBlend;
                }
                if (m.doubleSided)
                {
                    asset.source.cullMode =
                        foundation::materials::CullModeConfig::None;
                }

                content::Instance* inst = ClaimInstance(
                    group,
                    sel.SelectionName(pipeline::ImportResourceKind::Material, matBase.AsView()),
                    pipeline::MaterialAsset::StaticType(), claimed);
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
                        ? textureGuids[static_cast<usize>(tIdx)]
                        : Guid{});
            }
        }

        // The importer's deterministic skeleton base name (also the review plan's key).
        [[nodiscard]] static String SkeletonBaseName(const foundation::model::ModelSkin& skin)
        {
            return skin.name().IsEmpty() ? String(u8"skeleton")
                                         : ImportedAssetName(skin.name(), u8"skeleton", 0);
        }

        static void ImportSkeletonAndClips(const foundation::model::Model& model,
                                           content::Group& group, ModelManifestSource& manifest,
                                           Array<String>& claimed,
                                           const pipeline::ImportOptions& sel)
        {
            if (model.skins().Size() == 0)
            {
                return;
            }
            const foundation::model::ModelSkin& skin = *model.skins()[0];
            const HashMap<i32, i32> boneToJoint = BuildBoneToJoint(skin);

            const String skelBase = SkeletonBaseName(skin);
            if (sel.SelectionEnabled(pipeline::ImportResourceKind::Skeleton, skelBase.AsView()))
            {
                pipeline::SkeletonAsset skeleton;
                SkeletonSourceFromModel(model, skin, boneToJoint, skeleton.source);
                content::Instance* skelInst = ClaimInstance(
                    group,
                    sel.SelectionName(pipeline::ImportResourceKind::Skeleton, skelBase.AsView()),
                    pipeline::SkeletonAsset::StaticType(), claimed);
                if (skelInst != nullptr && skelInst->WriteObject(skeleton).IsOk())
                {
                    manifest.skeletonGuid = skelInst->Id();
                }
            }

            const Span<foundation::model::ModelAnimation* const> animations = model.animations();
            for (usize a = 0; a < animations.Size(); ++a)
            {
                const String clipBase = ImportedAssetName(animations[a]->name(), u8"anim", a);
                if (!sel.SelectionEnabled(pipeline::ImportResourceKind::AnimationClip,
                                          clipBase.AsView()))
                {
                    continue; // animationGuids is a plain list - no slot to hold
                }
                content::Instance* clipInst = ClaimInstance(
                    group,
                    sel.SelectionName(pipeline::ImportResourceKind::AnimationClip,
                                      clipBase.AsView()),
                    pipeline::AnimationClipAsset::StaticType(), claimed);
                pipeline::AnimationClipAsset clip;
                AnimationClipSourceFromModel(
                    *animations[a], boneToJoint,
                    (clipInst != nullptr) ? clipInst->Name() : StringView(u8"anim"), clip.source);
                if (clipInst != nullptr && clipInst->WriteObject(clip).IsOk())
                {
                    manifest.animationGuids.PushBack(clipInst->Id());
                }
            }
        }

        // Authored LOD collapse: "Foo_LOD1"/"Foo_LOD2" meshes become chain levels of the mesh
        // named "Foo" instead of assets of their own. lodOf[i] = the base mesh index a
        // suffixed mesh folds into (or -1); levels are gathered per base sorted by their
        // suffix number. A MIXED pair (skinned base with a static level or vice versa) is
        // refused - the parallel-stream contract cannot hold across the mismatch. Shared by
        // the import fan-out and DescribeImport so the review plan lists EXACTLY the mesh
        // assets the import creates.
        static void ComputeLodFold(const foundation::model::Model& model, bool logMismatch,
                                   Array<i32>& lodOf, Array<Array<usize>>& lodLevels)
        {
            const bool hasSkin = model.skins().Size() > 0;
            const Span<foundation::model::ModelMesh* const> meshes = model.meshes();
            lodOf.Resize(meshes.Size());
            lodLevels.Resize(meshes.Size());
            for (usize i = 0; i < meshes.Size(); ++i)
            {
                lodOf[i] = -1;
            }
            for (usize i = 0; i < meshes.Size(); ++i)
            {
                String lodBase;
                const u32 level = pipeline::ParseLodSuffix(StringView(meshes[i]->name()), lodBase);
                if (level == 0)
                {
                    continue;
                }
                i32 baseIndex = -1;
                for (usize j = 0; j < meshes.Size(); ++j)
                {
                    if (j != i && StringView(meshes[j]->name()) == lodBase.AsView())
                    {
                        baseIndex = static_cast<i32>(j);
                        break;
                    }
                }
                if (baseIndex < 0)
                {
                    continue; // no base of that name - a plain mesh that happens to end _LODn
                }
                if ((IsSkinnedMesh(*meshes[baseIndex]) && hasSkin) !=
                    (IsSkinnedMesh(*meshes[i]) && hasSkin))
                {
                    if (logMismatch)
                    {
                        LOG_WARNING(u8"Import",
                                    u8"mesh '{}': LOD level and base disagree on skinning - "
                                    u8"importing as a separate mesh",
                                    meshes[i]->name());
                    }
                    continue;
                }
                lodOf[i] = baseIndex;
                // Insert sorted by suffix number so LOD2 lands after LOD1 regardless of node order.
                Array<usize>& levels = lodLevels[static_cast<usize>(baseIndex)];
                String otherBase;
                usize at = levels.Size();
                for (usize k = 0; k < levels.Size(); ++k)
                {
                    if (level < pipeline::ParseLodSuffix(StringView(meshes[levels[k]]->name()),
                                                        otherBase))
                    {
                        at = k;
                        break;
                    }
                }
                levels.Insert(at, i);
            }
        }

        [[nodiscard]] static Status ImportMeshes(const foundation::model::Model& model,
                                                 content::Group& group,
                                                 ModelManifestSource& manifest,
                                                 Array<String>& claimed,
                                                 Array<pipeline::DeferredImportWrite>* deferredWrites,
                                                 bool generateLods,
                                                 const pipeline::ImportOptions& sel,
                                                 Array<String>& meshSourceNames)
        {
            const bool hasSkin = model.skins().Size() > 0;
            const Span<foundation::model::ModelMesh* const> meshes = model.meshes();
            Array<i32> lodOf;
            Array<Array<usize>> lodLevels; // per mesh: consumed level indices, suffix order
            ComputeLodFold(model, /*logMismatch*/ true, lodOf, lodLevels);

            for (usize i = 0; i < meshes.Size(); ++i)
            {
                if (lodOf[i] >= 0)
                {
                    // Consumed as a chain level of its base - no asset of its own, but the
                    // manifest slot MUST hold (nil guid): node.meshIndex is a MODEL mesh
                    // index, and dropping the entry shifted every later mesh's slot - nodes
                    // then referenced the wrong mesh or fell out of range, and the generated
                    // prefab/scene silently lost meshes on authored-LOD models.
                    const foundation::model::ModelMesh& folded = *meshes[i];
                    manifest.meshGuids.PushBack(Guid{});
                    manifest.meshSkinned.PushBack(
                        (IsSkinnedMesh(folded) && hasSkin) ? u8{1} : u8{0});
                    const Span<const foundation::model::ModelMeshPart> foldedParts =
                        folded.parts();
                    manifest.meshMaterial.PushBack(
                        foldedParts.Size() > 0 ? foldedParts[0].materialIndex : -1);
                    meshSourceNames.PushBack(ImportedAssetName(folded.name(), u8"mesh", i));
                    continue;
                }
                const foundation::model::ModelMesh& m = *meshes[i];
                const bool skinned = IsSkinnedMesh(m) && hasSkin;
                const String baseName = ImportedAssetName(m.name(), u8"mesh", i);
                if (!sel.SelectionEnabled(pipeline::ImportResourceKind::Mesh, baseName.AsView()))
                {
                    // Deselected in the review dialog: hold the manifest slot (nil guid) so
                    // parallel arrays and node mesh indices keep their shape.
                    manifest.meshGuids.PushBack(Guid{});
                    manifest.meshSkinned.PushBack(skinned ? u8{1} : u8{0});
                    const Span<const foundation::model::ModelMeshPart> skippedParts = m.parts();
                    manifest.meshMaterial.PushBack(
                        skippedParts.Size() > 0 ? skippedParts[0].materialIndex : -1);
                    meshSourceNames.PushBack(baseName);
                    continue;
                }
                const StringView name =
                    sel.SelectionName(pipeline::ImportResourceKind::Mesh, baseName.AsView());

                // Mesh envelopes are the import's largest SERIALIZATION cost (a big mesh
                // source rendered to XML) - defer object + write to the worker flush.
                content::Instance* inst = nullptr;
                Status written;
                if (skinned)
                {
                    auto asset = MakeRef<pipeline::SkinnedMeshAsset>(DefaultAllocator());
                    SkinnedMeshSourceFromModel(m, 0, asset->source);
                    // Authored _LODn levels fold into the chain (skinned overload keeps the
                    // parallel skinning stream in lockstep); big chainless meshes auto-generate
                    // (simplification only drops indices - the skin stream is untouched).
                    for (const usize levelIndex : lodLevels[i])
                    {
                        if (!pipeline::AppendLodLevelFromModel(*meshes[levelIndex],
                                                               asset->source))
                        {
                            LOG_WARNING(u8"Import",
                                        u8"mesh '{}': LOD level '{}' mismatched (submesh count "
                                        u8"or skin stream) - level skipped",
                                        m.name(), meshes[levelIndex]->name());
                        }
                    }
                    if (asset->source.lodCount > 1)
                    {
                        LOG_INFO(u8"Import", u8"mesh '{}': authored LOD chain with {} level(s)",
                                 m.name(), asset->source.lodCount);
                    }
                    else if (generateLods &&
                             asset->source.indexData.Size() >= 3u * 10000u)
                    {
                        (void)pipeline::GenerateLodChain(asset->source);
                    }
                    inst = ClaimInstance(
                        group, name, pipeline::SkinnedMeshAsset::StaticType(), claimed);
                    if (inst == nullptr)
                    {
                        return Status{ErrorCode::Unknown};
                    }
                    if (deferredWrites != nullptr)
                    {
                        // Sidecar split (v3): tiny envelope + binary geometry stream, both
                        // deferred. The binary serialize is cheap (the XML rendering was the
                        // cost this defers); bytes are owned by the deferred write.
                        asset->geometryInSidecar = true;
                        pipeline::DeferredImportWrite envelope;
                        envelope.instance = inst;
                        envelope.object = RefPtr<ISerializable>(asset.Get());
                        deferredWrites->PushBack(
                            static_cast<pipeline::DeferredImportWrite&&>(envelope));
                        pipeline::DeferredImportWrite geometry;
                        geometry.instance = inst;
                        geometry.streamName = String(pipeline::kMeshGeometryStreamName);
                        pipeline::detail::MeshSourceToBytes(
                            asset->source, *asset->GetType(), geometry.owned);
                        deferredWrites->PushBack(
                            static_cast<pipeline::DeferredImportWrite&&>(geometry));
                    }
                    else
                    {
                        written = pipeline::WriteMeshAsset(*inst, *asset);
                    }
                }
                else
                {
                    auto asset = MakeRef<pipeline::StaticMeshAsset>(DefaultAllocator());
                    StaticMeshSourceFromModel(m, asset->source);
                    // Fold the gathered _LODn siblings into this asset's chain (suffix order).
                    for (const usize levelIndex : lodLevels[i])
                    {
                        if (!pipeline::AppendLodLevelFromModel(*meshes[levelIndex],
                                                               asset->source))
                        {
                            LOG_WARNING(u8"Import",
                                        u8"mesh '{}': LOD level '{}' has a different submesh "
                                        u8"count - level skipped",
                                        m.name(), meshes[levelIndex]->name());
                        }
                    }
                    if (asset->source.lodCount > 1)
                    {
                        LOG_INFO(u8"Import", u8"mesh '{}': authored LOD chain with {} level(s)",
                                 m.name(), asset->source.lodCount);
                    }
                    // Auto-generation: big static meshes with NO authored
                    // chain get a simplified ladder (GenerateLodChain no-ops on chains).
                    else if (generateLods &&
                             asset->source.indexData.Size() >= 3u * 10000u)
                    {
                        (void)pipeline::GenerateLodChain(asset->source);
                    }
                    inst = ClaimInstance(
                        group, name, pipeline::StaticMeshAsset::StaticType(), claimed);
                    if (inst == nullptr)
                    {
                        return Status{ErrorCode::Unknown};
                    }
                    if (deferredWrites != nullptr)
                    {
                        asset->geometryInSidecar = true; // sidecar split (v3) - see skinned branch
                        pipeline::DeferredImportWrite envelope;
                        envelope.instance = inst;
                        envelope.object = RefPtr<ISerializable>(asset.Get());
                        deferredWrites->PushBack(
                            static_cast<pipeline::DeferredImportWrite&&>(envelope));
                        pipeline::DeferredImportWrite geometry;
                        geometry.instance = inst;
                        geometry.streamName = String(pipeline::kMeshGeometryStreamName);
                        pipeline::detail::MeshSourceToBytes(
                            asset->source, *asset->GetType(), geometry.owned);
                        deferredWrites->PushBack(
                            static_cast<pipeline::DeferredImportWrite&&>(geometry));
                    }
                    else
                    {
                        written = pipeline::WriteMeshAsset(*inst, *asset);
                    }
                }
                if (!written.IsOk())
                {
                    return written;
                }

                manifest.meshGuids.PushBack(inst->Id());
                manifest.meshSkinned.PushBack(skinned ? u8{1} : u8{0});
                const Span<const foundation::model::ModelMeshPart> parts = m.parts();
                manifest.meshMaterial.PushBack(parts.Size() > 0 ? parts[0].materialIndex : -1);
                meshSourceNames.PushBack(baseName);
            }
            return Status{};
        }

        // One CollisionShapeAsset per STATIC mesh (skinned meshes don't get collision);
        // the guid lands in manifest.collisionGuids (parallel; nil = none) for the
        // prefab generator to wire colliders from.
        static void ImportCollisionShapes(content::Group& group, ModelManifestSource& manifest,
                                          bool convex, Array<String>& claimed,
                                          const pipeline::ImportOptions& sel,
                                          const Array<String>& meshSourceNames)
        {
            for (usize i = 0; i < manifest.meshGuids.Size(); ++i)
            {
                if (manifest.meshSkinned[i] != 0 || manifest.meshGuids[i].IsNil())
                {
                    manifest.collisionGuids.PushBack(Guid{});
                    continue;
                }
                // Plan key: the MESH's source base + ".collision" (stable under mesh renames -
                // the created name below still derives from the live mesh instance name, so a
                // renamed mesh cascades to its shape's name).
                String selKey(i < meshSourceNames.Size() ? meshSourceNames[i].AsView()
                                                         : StringView(u8"mesh"));
                selKey.Append(u8".collision");
                if (!sel.SelectionEnabled(pipeline::ImportResourceKind::Collision,
                                          selKey.AsView()))
                {
                    manifest.collisionGuids.PushBack(Guid{});
                    continue;
                }
                content::Instance* meshInstance = nullptr;
                for (content::Instance* candidate : group.Instances())
                {
                    if (candidate->Id() == manifest.meshGuids[i])
                    {
                        meshInstance = candidate;
                        break;
                    }
                }
                String name(meshInstance != nullptr ? meshInstance->Name() : StringView(u8"mesh"));
                name.Append(u8".collision");
                const StringView renamed =
                    sel.SelectionName(pipeline::ImportResourceKind::Collision, selKey.AsView());
                if (renamed != selKey.AsView())
                {
                    name = String(renamed); // explicit user rename wins over the derived name
                }
                content::Instance* inst =
                    ClaimInstance(group, name.AsView(),
                                  pipeline::CollisionShapeAsset::StaticType(), claimed);
                if (inst == nullptr)
                {
                    manifest.collisionGuids.PushBack(Guid{});
                    continue;
                }
                pipeline::CollisionShapeAsset asset;
                asset.sourceMesh = manifest.meshGuids[i];
                asset.cook = convex ? pipeline::CollisionCookKind::ConvexHull
                                    : pipeline::CollisionCookKind::TriangleMesh;
                if (!inst->WriteObject(asset).IsOk())
                {
                    manifest.collisionGuids.PushBack(Guid{});
                    continue;
                }
                manifest.collisionGuids.PushBack(inst->Id());
            }
        }

        static void ImportNodes(const foundation::model::Model& model, ModelManifestSource& manifest)
        {
            const Span<foundation::model::ModelBone* const> bones = model.bones();
            for (usize i = 0; i < bones.Size(); ++i)
            {
                const foundation::model::ModelBone& b = *bones[i];
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

    RTTI_DEFINE_OBJECT(ModelManifestAsset, "rtti::pipeline::modelimporter")
    RTTI_DEFINE_OBJECT(ModelImportOptions, "rtti::pipeline::modelimporter")
    RTTI_DEFINE_OBJECT(LoadedModel, "rtti::pipeline::modelimporter")
}
