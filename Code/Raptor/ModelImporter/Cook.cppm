/// Raptor::ModelImporter:cook — cook a loaded Model into a content database.
///
/// Converts each Model mesh into a cooked geometry Source written through the editor
/// stack (StaticMeshAsset -> builder -> content Instance), then writes a manifest
/// (ModelManifestSource: the mesh Guids + node hierarchy) as its own instance and
/// returns that manifest's Guid. The runtime binds the manifest as a ModelResource
/// (a composite that pulls in the meshes via dependency edges) and spawns from it.
///
/// v1 cooks every mesh as STATIC (skinned meshes render in bind pose) — the skinned
/// source + skeleton + the GPU skinning path land in a later step. Materials/textures
/// are likewise added next; for now meshes spawn with the renderer's default material.

module;
#include "Core/Prelude.h"

export module raptor.modelimporter:cook;

import raptor.core;
import raptor.model;
import raptor.geometry;
import raptor.geometry.resource;
import raptor.geometry.editor;
import raptor.content;
import raptor.editor;
import :mesh_convert;
import :resource;

using namespace raptor::core;
namespace mdl = raptor::model;
namespace geo = raptor::geometry;
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

// Cook all meshes (as static) + the node hierarchy into outDb, writing a manifest
// instance. `namePrefix` namespaces the created content instances (e.g. "Duck").
// On success `outModelGuid` is the manifest (ModelResource) Guid to Bind at runtime.
[[nodiscard]] inline Status CookModel(const mdl::Model& model, ct::ContentDatabase& outDb,
                                      StringView namePrefix, Guid& outModelGuid)
{
    ct::Group* root = outDb.RootGroup();
    if (root == nullptr) { return Status{ ErrorCode::Unknown }; }

    ModelManifestSource manifest;
    const_cast<mdl::Model&>(model).calculateBounds();   // ensure model-space AABB is populated
    manifest.boundsMin = model.bounds().min;
    manifest.boundsMax = model.bounds().max;
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

    // Write the manifest as its own instance (the ModelResource the runtime binds).
    const String manifestName = Format(u8"{}.model", namePrefix);
    ct::Instance* manifestInst = root->CreateInstance(manifestName.AsView(), ModelManifestSource::StaticType());
    if (manifestInst == nullptr) { return Status{ ErrorCode::Unknown }; }
    const Status ms = manifestInst->WriteObject(manifest);
    if (!ms.IsOk()) { return ms; }

    outModelGuid = manifestInst->Id();
    return Status{};
}

} // namespace raptor::modelimporter
