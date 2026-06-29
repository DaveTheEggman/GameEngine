/// Raptor::ModelImporter:cook — cook a loaded Model into a content database.
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

export module raptor.modelimporter:cook;

import raptor.core;
import raptor.rhi;
import raptor.model;
import raptor.geometry;
import raptor.geometry.resource;
import raptor.geometry.editor;
import raptor.materials;
import raptor.materials.resource;
import raptor.materials.editor;
import raptor.texture.resource;
import raptor.content;
import raptor.editor;
import :mesh_convert;
import :resource;

using namespace raptor::core;
namespace rhi = raptor::rhi;
namespace mdl = raptor::model;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;
namespace tex = raptor::texture;
namespace ct  = raptor::content;
namespace ed  = raptor::editor;

export namespace raptor::modelimporter {

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

    geo::StaticMeshAssetBuilder meshBuilder;
    const Span<mdl::ModelMesh* const> meshes = model.meshes();
    for (usize i = 0; i < meshes.Size(); ++i) {
        const mdl::ModelMesh& m = *meshes[i];

        geo::StaticMeshAsset asset;
        StaticMeshSourceFromModel(m, asset.source);

        const String name = Format(u8"{}.mesh.{}", namePrefix, i);
        ct::Instance* inst = root->CreateInstance(name.AsView(), geo::StaticMeshSource::StaticType());
        if (inst == nullptr) { return Status{ ErrorCode::Unknown }; }
        ed::AssetBuildContext ctx{ StringView{}, inst };
        const Status s = meshBuilder.Build(asset, ctx);
        if (!s.IsOk()) { return s; }

        manifest.meshGuids.PushBack(inst->Id());
        manifest.meshSkinned.PushBack(IsSkinnedMesh(m) ? u8{ 1 } : u8{ 0 });
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

} // namespace raptor::modelimporter
