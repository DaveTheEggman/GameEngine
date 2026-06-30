/// Draconic::ModelImporter:cook — cook a loaded Model into a content database.
///
/// Cooks a model's textures, materials, and meshes through the editor stack (Asset ->
/// builder -> content Instance) into the output DB, then writes a manifest
/// (ModelManifestSource: resource Guids + node hierarchy + cross-refs) and returns its
/// Guid. The runtime binds the manifest as a ModelResource (a composite pulling in the
/// meshes + materials via dependency edges) and spawns from it.
///
/// v1 cooks every mesh as STATIC (skinned meshes render in bind pose) and binds the
/// albedo texture per material (other PBR maps + skinning land next). Materials use the
/// renderer's builtin "forward" shader by name (no cooked ShaderResource needed yet).

module;
#include "Core/Prelude.h"

export module draconic.modelimporter:cook;

import draconic.core;
import draconic.rhi;
import draconic.model;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.geometry.editor;
import draconic.materials;
import draconic.materials.resource;
import draconic.materials.editor;
import draconic.texture.resource;
import draconic.animation;
import draconic.animation.resource;
import draconic.animation.editor;
import draconic.content;
import draconic.editor;
import :mesh_convert;
import :anim_convert;
import :resource;

using namespace draconic::core;
namespace rhi  = draconic::rhi;
namespace mdl  = draconic::model;
namespace geo  = draconic::geometry;
namespace mat  = draconic::materials;
namespace tex  = draconic::texture;
namespace anim = draconic::animation;
namespace ct   = draconic::content;
namespace ed   = draconic::editor;

export namespace draconic::modelimporter {

// True if the model mesh carries skinning (Joints/Weights vertex elements).
[[nodiscard]] inline bool IsSkinnedMesh(const mdl::ModelMesh& mesh) noexcept
{
    for (const mdl::VertexElement& e : mesh.vertexElements()) {
        if (e.semantic == mdl::VertexSemantic::Joints) { return true; }
    }
    return false;
}

// Cook the model's textures into outDb. The model loaders DECODE every texture into raw
// RGBA8 pixels (storeImageData) for external files, data-URIs, AND embedded GLB buffer-views
// alike, so we cook directly from ModelTexture's pixel bytes — no file re-read, and embedded
// textures work. Returns one Guid per model texture (nil if it has no usable RGBA8 data).
inline void CookTextures(const mdl::Model& model, ct::Group* root, StringView namePrefix, Array<Guid>& outGuids)
{
    const Span<mdl::ModelTexture* const> textures = model.textures();
    for (usize i = 0; i < textures.Size(); ++i) {
        const mdl::ModelTexture& t = *textures[i];
        const u8* data = t.getData();
        const i32 size = t.getDataSize();
        // The loaders decode to RGBA8 (4 bpp); guard against any other layout for now.
        const bool rgba8 = (data != nullptr && t.width > 0 && t.height > 0 && size == t.width * t.height * 4);
        if (!rgba8) { outGuids.PushBack(Guid{}); continue; }

        tex::TextureResource res;
        res.width  = static_cast<u32>(t.width);
        res.height = static_cast<u32>(t.height);
        res.format = rhi::TextureFormat::RGBA8UnormSrgb;   // base-color textures are sRGB-encoded
        res.mipLevels = 1;                                 // factory uploads mip 0 (no mip gen yet)
        res.generateMipmaps = false;

        const String name = Format(u8"{}.tex.{}", namePrefix, i);
        ct::Instance* inst = root->CreateInstance(name.AsView(), tex::TextureResource::StaticType());
        if (inst == nullptr) { outGuids.PushBack(Guid{}); continue; }
        if (!inst->WriteObject(res).IsOk()) { outGuids.PushBack(Guid{}); continue; }
        const Status ds = inst->WriteData(u8"data", Span<const byte>{ reinterpret_cast<const byte*>(data), static_cast<usize>(size) });
        outGuids.PushBack(ds.IsOk() ? inst->Id() : Guid{});
    }
}

// Cook the model's materials into outDb (as MaterialSource via CreatePBR + the editor cook).
// Records, per material, its cooked Guid + the albedo texture Guid (resolved from textureGuids).
inline void CookMaterials(const mdl::Model& model, ct::Group* root, StringView namePrefix,
                          const Array<Guid>& textureGuids, Array<Guid>& outMatGuids, Array<Guid>& outAlbedo)
{
    mat::MaterialAssetBuilder builder;
    const Span<mdl::ModelMaterial* const> materials = model.materials();
    for (usize i = 0; i < materials.Size(); ++i) {
        const mdl::ModelMaterial& m = *materials[i];

        // Build a standard PBR material carrying the model's factors, capture it into a source that
        // names the builtin "forward" shader (no cooked ShaderResource needed).
        RefPtr<mat::Material> built = mat::CreatePBR(Format(u8"{}.mat.{}", namePrefix, i).AsView(),
                                                     m.baseColorFactor, m.metallicFactor, m.roughnessFactor);
        mat::MaterialAsset asset;
        mat::MaterialImporter::Import(*built, Guid{}, asset);   // nil shaderId -> use shaderName
        asset.source.shaderName = String(u8"forward");

        const String name = Format(u8"{}.mat.{}", namePrefix, i);
        ct::Instance* inst = root->CreateInstance(name.AsView(), mat::MaterialSource::StaticType());
        if (inst == nullptr) { outMatGuids.PushBack(Guid{}); outAlbedo.PushBack(Guid{}); continue; }
        ed::AssetBuildContext ctx{ StringView{}, inst };
        if (builder.Build(asset, ctx).IsOk()) { outMatGuids.PushBack(inst->Id()); }
        else                                  { outMatGuids.PushBack(Guid{}); }

        const i32 tIdx = m.baseColorTextureIndex;
        outAlbedo.PushBack((tIdx >= 0 && static_cast<usize>(tIdx) < textureGuids.Size())
                               ? textureGuids[static_cast<usize>(tIdx)] : Guid{});
    }
}

// Cook a model into outDb (textures + materials + static meshes + manifest). `namePrefix`
// namespaces the created instances. On success `outModelGuid` is the manifest
// (ModelResource) Guid to Bind at runtime.
[[nodiscard]] inline Status CookModel(const mdl::Model& model, ct::ContentDatabase& outDb,
                                      StringView namePrefix, Guid& outModelGuid)
{
    ct::Group* root = outDb.RootGroup();
    if (root == nullptr) { return Status{ ErrorCode::Unknown }; }

    ModelManifestSource manifest;
    const_cast<mdl::Model&>(model).calculateBounds();   // ensure model-space AABB is populated
    manifest.boundsMin = model.bounds().min;
    manifest.boundsMax = model.bounds().max;

    Array<Guid> textureGuids;
    CookTextures(model, root, namePrefix, textureGuids);
    CookMaterials(model, root, namePrefix, textureGuids, manifest.materialGuids, manifest.materialAlbedo);

    // Skeleton + animations (skin 0 only for now). The skeleton's bone order = the skin's joint order;
    // animation channels + bone parents are remapped from model bone indices into joint indices.
    HashMap<i32, i32> boneToJoint;
    const bool hasSkin = model.skins().Size() > 0;
    if (hasSkin) {
        const mdl::ModelSkin& skin = *model.skins()[0];
        boneToJoint = BuildBoneToJoint(skin);

        anim::SkeletonAsset skelAsset;
        SkeletonSourceFromModel(model, skin, boneToJoint, skelAsset.source);
        anim::SkeletonAssetBuilder skelBuilder;
        ct::Instance* skelInst = root->CreateInstance(Format(u8"{}.skeleton", namePrefix).AsView(), anim::SkeletonSource::StaticType());
        if (skelInst != nullptr) {
            ed::AssetBuildContext ctx{ StringView{}, skelInst };
            if (skelBuilder.Build(skelAsset, ctx).IsOk()) { manifest.skeletonGuid = skelInst->Id(); }
        }

        anim::AnimationClipAssetBuilder clipBuilder;
        const Span<mdl::ModelAnimation* const> animations = model.animations();
        for (usize a = 0; a < animations.Size(); ++a) {
            anim::AnimationClipAsset clipAsset;
            AnimationClipSourceFromModel(*animations[a], boneToJoint, Format(u8"{}.anim.{}", namePrefix, a).AsView(), clipAsset.source);
            ct::Instance* clipInst = root->CreateInstance(Format(u8"{}.anim.{}", namePrefix, a).AsView(), anim::AnimationClipSource::StaticType());
            if (clipInst == nullptr) { continue; }
            ed::AssetBuildContext ctx{ StringView{}, clipInst };
            if (clipBuilder.Build(clipAsset, ctx).IsOk()) { manifest.animationGuids.PushBack(clipInst->Id()); }
        }
    }

    geo::StaticMeshAssetBuilder  meshBuilder;
    geo::SkinnedMeshAssetBuilder skinnedBuilder;
    const Span<mdl::ModelMesh* const> meshes = model.meshes();
    for (usize i = 0; i < meshes.Size(); ++i) {
        const mdl::ModelMesh& m = *meshes[i];
        const bool skinned = IsSkinnedMesh(m) && hasSkin;
        const String name = Format(u8"{}.mesh.{}", namePrefix, i);

        ct::Instance* inst = root->CreateInstance(name.AsView(),
            skinned ? geo::SkinnedMeshSource::StaticType() : geo::StaticMeshSource::StaticType());
        if (inst == nullptr) { return Status{ ErrorCode::Unknown }; }
        ed::AssetBuildContext ctx{ StringView{}, inst };

        Status s;
        if (skinned) {
            geo::SkinnedMeshAsset asset;
            SkinnedMeshSourceFromModel(m, /*skeletonIndex*/ 0, asset.source);
            s = skinnedBuilder.Build(asset, ctx);
        } else {
            geo::StaticMeshAsset asset;
            StaticMeshSourceFromModel(m, asset.source);
            s = meshBuilder.Build(asset, ctx);
        }
        if (!s.IsOk()) { return s; }

        manifest.meshGuids.PushBack(inst->Id());
        manifest.meshSkinned.PushBack(skinned ? u8{ 1 } : u8{ 0 });
        // One material per mesh for now: the first submesh's material (single-material models).
        const Span<const mdl::ModelMeshPart> parts = m.parts();
        manifest.meshMaterial.PushBack(parts.Size() > 0 ? parts[0].materialIndex : -1);
    }

    const Span<mdl::ModelBone* const> bones = model.bones();
    for (usize i = 0; i < bones.Size(); ++i) {
        const mdl::ModelBone& b = *bones[i];
        ModelNode n;
        n.name                    = String(b.name());
        n.parentIndex             = b.parentIndex;
        n.localTransform.position = b.translation;
        n.localTransform.rotation = b.rotation;
        n.localTransform.scale    = b.scale;
        n.meshIndex               = b.meshIndex;
        manifest.nodes.PushBack(Move(n));
    }

    const String manifestName = Format(u8"{}.model", namePrefix);
    ct::Instance* manifestInst = root->CreateInstance(manifestName.AsView(), ModelManifestSource::StaticType());
    if (manifestInst == nullptr) { return Status{ ErrorCode::Unknown }; }
    const Status ms = manifestInst->WriteObject(manifest);
    if (!ms.IsOk()) { return ms; }

    outModelGuid = manifestInst->Id();
    return Status{};
}

} // namespace draconic::modelimporter
