/// Draconic::ModelImporter:resource — the imported model as a cooked composite resource.
///
/// A model import produces many leaf resources (meshes, later materials/textures/
/// skeleton/animations) PLUS a manifest tying them together with the node hierarchy.
/// The manifest is itself a resource: `ModelManifestSource` (authored/cooked data) is
/// built by `ModelFactory` into a runtime `ModelResource` that resolves every leaf via
/// manager.Bind (so the model->mesh edges are recorded automatically, Traktor-style).
/// The runtime binds ONE ModelResource and instantiates its node hierarchy — like a
/// prefab. Spawning into a scene lives in the app/engine (keeps scene/render deps out).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.modelimporter:resource;

import draconic.core;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.materials;
import draconic.materials.resource;
import draconic.texture;
import draconic.texture.resource;
import draconic.animation;
import draconic.animation.resource;
import draconic.resource;
import draconic.content;

using namespace draconic::core;
using namespace draconic::resource;
namespace geo  = draconic::geometry;
namespace mat  = draconic::materials;
namespace tex  = draconic::texture;
namespace anim = draconic::animation;

export namespace draconic::modelimporter {

// One node of the imported hierarchy: local TRS + an optional mesh reference (index into
// the manifest's mesh list, -1 = no mesh). Plain copyable struct (free Serialize via ADL).
struct ModelNode {
    String    name;
    i32       parentIndex = -1;
    Transform localTransform;
    i32       meshIndex = -1;
};

// Free Serialize for ModelNode (ADL-found by the generic Array<T> serializer).
inline void Serialize(ISerializer& ar, ModelNode& n)
{
    draconic::core::Serialize(ar, "name",   n.name);
    draconic::core::Serialize(ar, "parent", n.parentIndex);
    draconic::core::Serialize(ar, "t",      n.localTransform.position);
    draconic::core::Serialize(ar, "r",      n.localTransform.rotation);
    draconic::core::Serialize(ar, "s",      n.localTransform.scale);
    draconic::core::Serialize(ar, "mesh",   n.meshIndex);
}

// Authored/cooked manifest: the leaf resource Guids + the node hierarchy.
class ModelManifestSource final : public ISerializable {
    DRACONIC_OBJECT(ModelManifestSource, ISerializable)
public:
    Array<Guid>      meshGuids;     // cooked mesh resources
    Array<u8>        meshSkinned;   // 1 if the mesh is skinned (parallel to meshGuids)
    Array<i32>       meshMaterial;  // material index per mesh (-1 = none); parallel to meshGuids
    Array<Guid>      materialGuids; // cooked material resources
    Array<Guid>      materialAlbedo;// albedo texture per material (nil = none); parallel to materialGuids
    Array<ModelNode> nodes;         // node hierarchy
    Guid             skeletonGuid;  // cooked skeleton (nil if the model has no skin)
    Array<Guid>      animationGuids;// cooked animation clips
    Vec3             boundsMin{};   // model-space AABB (for spawn-time auto-fit/placement)
    Vec3             boundsMax{};

    void Serialize(ISerializer& ar) override
    {
        draconic::core::Serialize(ar, "meshGuids",      meshGuids);
        draconic::core::Serialize(ar, "meshSkinned",    meshSkinned);
        draconic::core::Serialize(ar, "meshMaterial",   meshMaterial);
        draconic::core::Serialize(ar, "materialGuids",  materialGuids);
        draconic::core::Serialize(ar, "materialAlbedo", materialAlbedo);
        draconic::core::Serialize(ar, "nodes",          nodes);
        draconic::core::Serialize(ar, "skeletonGuid",   skeletonGuid);
        draconic::core::Serialize(ar, "animationGuids", animationGuids);
        draconic::core::Serialize(ar, "boundsMin",      boundsMin);
        draconic::core::Serialize(ar, "boundsMax",      boundsMax);
    }
};

// Runtime model: the node hierarchy + the resolved mesh proxies (parallel to the manifest
// mesh list). The owning ResourceManager keeps the leaf resources alive via the recorded
// dependency edges; the proxies see reloads transparently.
class ModelResource final : public Object {
    DRACONIC_OBJECT(ModelResource, Object)
public:
    Array<ModelNode>               nodes;
    Array<RefPtr<geo::StaticMesh>> meshes;         // base ptr (a SkinnedMesh upcasts here); skinned via meshSkinned
    Array<u8>                      meshSkinned;
    Array<i32>                     meshMaterial;   // material index per mesh (-1 = none)
    Array<Proxy<mat::Material>>    materials;      // resolved materials (albedo wired as default texture)
    Proxy<anim::Skeleton>          skeleton;       // resolved skeleton (null if not skinned)
    Array<Proxy<anim::AnimationClip>> animations; // resolved animation clips
    Vec3                           boundsMin{};
    Vec3                           boundsMax{};
};

// Builds a ModelResource from a ModelManifestSource: copies the hierarchy + resolves each
// mesh Guid via manager.Bind (recording the model->mesh dependency edge).
class ModelFactory final : public IResourceFactory {
public:
    [[nodiscard]] const TypeInfo* ProductType() const override { return &ModelResource::StaticType(); }

    [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager, draconic::content::Instance& instance) override
    {
        RefPtr<ISerializable> object = instance.ReadObject();
        ModelManifestSource* src = Cast<ModelManifestSource>(object.Get());
        if (src == nullptr) { return RefPtr<Object>{}; }

        RefPtr<ModelResource> model = MakeRef<ModelResource>(DefaultAllocator());
        for (const ModelNode& n : src->nodes)        { model->nodes.PushBack(n); }
        for (const u8 s : src->meshSkinned)          { model->meshSkinned.PushBack(s); }
        for (const i32 m : src->meshMaterial)        { model->meshMaterial.PushBack(m); }

        // Resolve meshes (skinned ones bind as SkinnedMesh, stored as the StaticMesh base; the renderer
        // checks IsSkinned() + uploads the skin stream). Bind records the model->mesh dependency edge.
        for (usize i = 0; i < src->meshGuids.Size(); ++i) {
            const bool skinned = (i < src->meshSkinned.Size() && src->meshSkinned[i] != 0);
            geo::StaticMesh* mesh = skinned
                ? static_cast<geo::StaticMesh*>(manager.Bind<geo::SkinnedMesh>(src->meshGuids[i]).Get())
                : manager.Bind<geo::StaticMesh>(src->meshGuids[i]).Get();
            model->meshes.PushBack(RefPtr<geo::StaticMesh>(mesh));
        }

        // Resolve the skeleton + animation clips (composite edges).
        if (!src->skeletonGuid.IsNil()) { model->skeleton = manager.Bind<anim::Skeleton>(src->skeletonGuid); }
        for (const Guid& g : src->animationGuids) { model->animations.PushBack(manager.Bind<anim::AnimationClip>(g)); }

        // Resolve materials + wire their albedo texture in as the material's default (the renderer's
        // per-material instance reads default textures, so no per-instance assignment is needed).
        for (usize i = 0; i < src->materialGuids.Size(); ++i) {
            Proxy<mat::Material> material = manager.Bind<mat::Material>(src->materialGuids[i]);
            if (material && i < src->materialAlbedo.Size() && !src->materialAlbedo[i].IsNil()) {
                Proxy<tex::Texture> albedo = manager.Bind<tex::Texture>(src->materialAlbedo[i]);
                if (albedo && albedo->View() != nullptr) {
                    material->SetDefaultTexture(u8"AlbedoMap", albedo->View());
                }
            }
            model->materials.PushBack(material);
        }

        model->boundsMin = src->boundsMin;
        model->boundsMax = src->boundsMax;
        return model;
    }
};

DRACONIC_DEFINE_OBJECT(ModelManifestSource, "draconic::modelimporter")
DRACONIC_DEFINE_OBJECT(ModelResource, "draconic::modelimporter")

// Registers the importer's serializable types (+ the geometry source types it cooks and
// reads back) with the global type + serializable registries. Registration is explicit in
// Draconic (DRACONIC_DEFINE_OBJECT only defines StaticType); call this once before binding cooked
// models so the content DB can polymorphically deserialize them. Idempotent.
inline void RegisterModelImporterTypes()
{
    GlobalTypeRegistry().Register(ModelManifestSource::StaticType());
    RegisterSerializable<ModelManifestSource>();
    GlobalTypeRegistry().Register(ModelResource::StaticType());

    GlobalTypeRegistry().Register(geo::StaticMeshSource::StaticType());
    RegisterSerializable<geo::StaticMeshSource>();
    GlobalTypeRegistry().Register(geo::SkinnedMeshSource::StaticType());
    RegisterSerializable<geo::SkinnedMeshSource>();
    GlobalTypeRegistry().Register(geo::StaticMesh::StaticType());
    GlobalTypeRegistry().Register(geo::SkinnedMesh::StaticType());
    RegisterSerializable<geo::SkinnedMeshSource>();

    GlobalTypeRegistry().Register(mat::MaterialSource::StaticType());
    RegisterSerializable<mat::MaterialSource>();
    GlobalTypeRegistry().Register(mat::Material::StaticType());

    GlobalTypeRegistry().Register(tex::TextureResource::StaticType());
    RegisterSerializable<tex::TextureResource>();
    GlobalTypeRegistry().Register(tex::Texture::StaticType());

    GlobalTypeRegistry().Register(anim::SkeletonSource::StaticType());
    RegisterSerializable<anim::SkeletonSource>();
    GlobalTypeRegistry().Register(anim::Skeleton::StaticType());
    GlobalTypeRegistry().Register(anim::AnimationClipSource::StaticType());
    RegisterSerializable<anim::AnimationClipSource>();
    GlobalTypeRegistry().Register(anim::AnimationClip::StaticType());
}

} // namespace draconic::modelimporter
