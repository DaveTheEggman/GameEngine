/// Raptor::ModelImporter:resource — the imported model as a cooked composite resource.
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

export module raptor.modelimporter:resource;

import raptor.core;
import raptor.geometry;
import raptor.geometry.resource;
import raptor.resource;
import raptor.content;

using namespace raptor::core;
using namespace raptor::resource;
namespace geo = raptor::geometry;

export namespace raptor::modelimporter {

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
    raptor::core::Serialize(ar, "name",   n.name);
    raptor::core::Serialize(ar, "parent", n.parentIndex);
    raptor::core::Serialize(ar, "t",      n.localTransform.position);
    raptor::core::Serialize(ar, "r",      n.localTransform.rotation);
    raptor::core::Serialize(ar, "s",      n.localTransform.scale);
    raptor::core::Serialize(ar, "mesh",   n.meshIndex);
}

// Authored/cooked manifest: the leaf resource Guids + the node hierarchy.
class ModelManifestSource final : public ISerializable {
    RAPTOR_OBJECT(ModelManifestSource, ISerializable)
public:
    Array<Guid>      meshGuids;     // cooked mesh resources
    Array<u8>        meshSkinned;   // 1 if the mesh is skinned (parallel to meshGuids)
    Array<ModelNode> nodes;         // node hierarchy
    Vec3             boundsMin{};   // model-space AABB (for spawn-time auto-fit/placement)
    Vec3             boundsMax{};

    void Serialize(ISerializer& ar) override
    {
        raptor::core::Serialize(ar, "meshGuids",   meshGuids);
        raptor::core::Serialize(ar, "meshSkinned", meshSkinned);
        raptor::core::Serialize(ar, "nodes",       nodes);
        raptor::core::Serialize(ar, "boundsMin",   boundsMin);
        raptor::core::Serialize(ar, "boundsMax",   boundsMax);
    }
};

// Runtime model: the node hierarchy + the resolved mesh proxies (parallel to the manifest
// mesh list). The owning ResourceManager keeps the leaf resources alive via the recorded
// dependency edges; the proxies see reloads transparently.
class ModelResource final : public Object {
    RAPTOR_OBJECT(ModelResource, Object)
public:
    Array<ModelNode>               nodes;
    Array<Proxy<geo::StaticMesh>>  meshes;
    Array<u8>                      meshSkinned;
    Vec3                           boundsMin{};
    Vec3                           boundsMax{};
};

// Builds a ModelResource from a ModelManifestSource: copies the hierarchy + resolves each
// mesh Guid via manager.Bind (recording the model->mesh dependency edge).
class ModelFactory final : public IResourceFactory {
public:
    [[nodiscard]] const TypeInfo* ProductType() const override { return &ModelResource::StaticType(); }

    [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager, raptor::content::Instance& instance) override
    {
        RefPtr<ISerializable> object = instance.ReadObject();
        ModelManifestSource* src = Cast<ModelManifestSource>(object.Get());
        if (src == nullptr) { return RefPtr<Object>{}; }

        RefPtr<ModelResource> model = MakeRef<ModelResource>(DefaultAllocator());
        for (const ModelNode& n : src->nodes)        { model->nodes.PushBack(n); }
        for (const u8 s : src->meshSkinned)          { model->meshSkinned.PushBack(s); }
        for (const Guid& g : src->meshGuids)         { model->meshes.PushBack(manager.Bind<geo::StaticMesh>(g)); }
        model->boundsMin = src->boundsMin;
        model->boundsMax = src->boundsMax;
        return model;
    }
};

RAPTOR_DEFINE_OBJECT(ModelManifestSource, "raptor::modelimporter")
RAPTOR_DEFINE_OBJECT(ModelResource, "raptor::modelimporter")

// Registers the importer's serializable types (+ the geometry source types it cooks and
// reads back) with the global type + serializable registries. Registration is explicit in
// Raptor (RAPTOR_DEFINE_OBJECT only defines StaticType); call this once before binding cooked
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
}

} // namespace raptor::modelimporter
