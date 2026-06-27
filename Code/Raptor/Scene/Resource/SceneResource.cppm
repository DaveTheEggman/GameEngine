/// Raptor::SceneResource — the `raptor.scene.resource` module.
///
/// Whole-scene serialization (the runtime LOAD side; the editor save side is in
/// raptor.scene.editor). A scene is stored as one content-DB Instance: a small
/// SceneDocument primary (the name, so the instance materializes + is discoverable)
/// plus a "scene" data stream holding the serialized world.
///
/// SerializeScene is the bidirectional core: it runs the entity / transform / component
/// outer-join over an ISerializer. Entities persist by Guid (parent links + component
/// owners are stored as Guids and relinked on load). Components are serialized by their
/// owning manager (the typed manager is the serializer — value components carry no
/// vtable) and routed back on load by a stable string type id. The thin "component
/// type -> manager" routing is the only scene-specific registry; the managers
/// themselves already exist on the target scene (injected via ISceneAware), so load
/// deserializes into them.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.scene.resource;

import raptor.core;
import raptor.content;
import raptor.scene;

using namespace raptor::core;

export namespace raptor::scene {

// Minimal primary object so a scene is a first-class content-DB instance (carries the
// name for discovery; the heavy world data lives in the "scene" data stream).
class SceneDocument final : public ISerializable {
    RAPTOR_OBJECT(SceneDocument, ISerializable)
public:
    String name;
    void Serialize(ISerializer& ar) override { raptor::core::Serialize(ar, "name", name); }
};

namespace detail {
    inline void SerializeGuid(ISerializer& ar, const char* key, Guid& g) {
        ar.Key(key);
        raptor::core::Serialize(ar, "hi", g.high);
        raptor::core::Serialize(ar, "lo", g.low);
    }
    inline void SerializeTransform(ISerializer& ar, Transform& t) {
        raptor::core::Serialize(ar, "pos", t.position);
        raptor::core::Serialize(ar, "rot", t.rotation);
        raptor::core::Serialize(ar, "scl", t.scale);
    }
}

// Bidirectional whole-scene serialization: scene name, the entity table + transform
// hierarchy, then components. On read, `scene` should be freshly created with its
// component managers already present (so component records route into their pools).
inline void SerializeScene(ISerializer& ar, Scene& scene) {
    const bool writing = ar.Mode() == SerializeMode::Write;

    // --- name ---
    String name = writing ? String(scene.Name()) : String{};
    raptor::core::Serialize(ar, "name", name);
    if (!writing) { scene.SetName(name.AsView()); }

    // --- entities (id, name, active, parent-id, local transform) ---
    u32 entityCount = 0;
    Array<EntityHandle> handles;
    if (writing) {
        scene.ForEachEntity([&](EntityHandle e) { handles.PushBack(e); });
        entityCount = static_cast<u32>(handles.Size());
    }
    ar.Key("entities");
    ar.BeginArray(entityCount);
    if (writing) {
        for (EntityHandle e : handles) {
            Guid id = scene.GetEntityId(e);
            String ename = String(scene.GetEntityName(e));
            u8 active = scene.IsActive(e) ? 1u : 0u;
            EntityHandle parent = scene.GetParent(e);
            Guid parentId = parent.IsAssigned() ? scene.GetEntityId(parent) : Guid{};
            Transform t = scene.GetLocalTransform(e);
            detail::SerializeGuid(ar, "id", id);
            raptor::core::Serialize(ar, "name", ename);
            raptor::core::Serialize(ar, "active", active);
            detail::SerializeGuid(ar, "parent", parentId);
            detail::SerializeTransform(ar, t);
        }
    } else {
        Array<Guid> ids;
        Array<Guid> parents;
        for (u32 i = 0; i < entityCount; ++i) {
            Guid id; String ename; u8 active = 0; Guid parentId; Transform t;
            detail::SerializeGuid(ar, "id", id);
            raptor::core::Serialize(ar, "name", ename);
            raptor::core::Serialize(ar, "active", active);
            detail::SerializeGuid(ar, "parent", parentId);
            detail::SerializeTransform(ar, t);
            EntityHandle h = scene.CreateEntity(id, ename.AsView());
            scene.SetActive(h, active != 0);
            scene.SetLocalTransform(h, t);
            ids.PushBack(id);
            parents.PushBack(parentId);
        }
        // relink parents now that every entity exists
        for (usize i = 0; i < ids.Size(); ++i) {
            if (parents[i] != Guid{}) {
                EntityHandle child = scene.FindEntity(ids[i]);
                EntityHandle parent = scene.FindEntity(parents[i]);
                if (child.IsAssigned() && parent.IsAssigned()) { scene.SetParent(child, parent); }
            }
        }
    }
    ar.EndArray();

    // --- components (owner-id, type-id, data) ---
    // NOTE: records are written inline; load requires the owning manager to be present
    // on `scene` (the normal case — managers are injected before load). Skipping an
    // unknown component type (length-prefixed records) is a future robustness item.
    u32 componentCount = 0;
    struct Record { ComponentManagerBase* manager; EntityHandle owner; };
    Array<Record> records;
    if (writing) {
        scene.ForEachManager([&](ComponentManagerBase& m) {
            if (!m.IsSerializable()) { return; }
            for (EntityHandle owner : m.OwnerHandles()) { records.PushBack(Record{ &m, owner }); }
        });
        componentCount = static_cast<u32>(records.Size());
    }
    ar.Key("components");
    ar.BeginArray(componentCount);
    if (writing) {
        for (Record& r : records) {
            Guid ownerId = scene.GetEntityId(r.owner);
            String typeId = String(r.manager->SerializationTypeId());
            detail::SerializeGuid(ar, "owner", ownerId);
            raptor::core::Serialize(ar, "type", typeId);
            r.manager->WriteComponent(ar, r.owner);
        }
    } else {
        for (u32 i = 0; i < componentCount; ++i) {
            Guid ownerId; String typeId;
            detail::SerializeGuid(ar, "owner", ownerId);
            raptor::core::Serialize(ar, "type", typeId);
            EntityHandle owner = scene.FindEntity(ownerId);
            ComponentManagerBase* manager = scene.FindManagerBySerializationId(typeId.AsView());
            if (owner.IsAssigned() && manager != nullptr) {
                manager->ReadComponent(ar, owner);
            }
        }
    }
    ar.EndArray();
}

// Loads a cooked scene's "scene" data stream into `scene` (which must already have its
// component managers). Returns NotFound if the stream is missing.
inline Status LoadScene(raptor::content::Instance& instance, Scene& scene) {
    UniquePtr<IStream> stream = instance.ReadData(u8"scene");
    if (stream.Get() == nullptr) { return Status{ ErrorCode::NotFound }; }
    BinarySerializer ser(*stream, SerializeMode::Read);
    SerializeScene(ser, scene);
    return Status{};
}

RAPTOR_DEFINE_OBJECT(SceneDocument, "raptor::scene")

} // namespace raptor::scene
