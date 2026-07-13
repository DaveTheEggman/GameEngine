/// Draconic::SceneResource - the `draconic.scene.resource` module.
///
/// Whole-scene serialization (the runtime LOAD side; the editor save side is in
/// draconic.scene.editor). A scene is stored as one content-DB Instance: a small
/// SceneDocument primary (the name, so the instance materializes + is discoverable)
/// plus a "scene" data stream holding the serialized world.
///
/// SerializeScene is the bidirectional core: it runs the entity / transform / component
/// outer-join over an ISerializer. Entities persist by Guid (parent links + component
/// owners are stored as Guids and relinked on load). Components are serialized by their
/// owning manager (the typed manager is the serializer - value components carry no
/// vtable) and routed back on load by a stable string type id. The thin "component
/// type -> manager" routing is the only scene-specific registry; the managers
/// themselves already exist on the target scene (injected via ISceneAware), so load
/// deserializes into them.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module draconic.scene.resource;

import draconic.core;
import draconic.resource;
import draconic.content;
import draconic.scene;

using namespace draconic::core;

export namespace draconic::scene {

// Minimal primary object so a scene is a first-class content-DB instance (carries the
// name for discovery; the heavy world data lives in the "scene" data stream).
class SceneDocument final : public ISerializable {
    DRACONIC_OBJECT(SceneDocument, ISerializable)
public:
    String name;
    void Serialize(ISerializer& ar) override { draconic::core::Serialize(ar, "name", name); }
};

namespace detail {
    inline void SerializeGuid(ISerializer& ar, const char* key, Guid& g) {
        ar.Key(key);
        draconic::core::Serialize(ar, "hi", g.high);
        draconic::core::Serialize(ar, "lo", g.low);
    }
    inline void SerializeTransform(ISerializer& ar, Transform& t) {
        draconic::core::Serialize(ar, "pos", t.position);
        draconic::core::Serialize(ar, "rot", t.rotation);
        draconic::core::Serialize(ar, "scl", t.scale);
    }
}

// Bidirectional whole-scene serialization: scene name, the entity table + transform
// hierarchy, then components. On read, `scene` should be freshly created with its
// component managers already present (so component records route into their pools).
inline void SerializeScene(ISerializer& ar, Scene& scene) {
    const bool writing = ar.Mode() == SerializeMode::Write;

    // --- name ---
    String name = writing ? String(scene.Name()) : String{};
    draconic::core::Serialize(ar, "name", name);
    if (!writing) { scene.SetName(name.AsView()); }

    // --- entities (id, name, active, parent-id, local transform) ---
    u32 entityCount = 0;
    Array<EntityHandle> handles;
    if (writing) {
        // TREE order (roots in list order, depth-first children): load recreates entities and
        // relinks parents in FILE order, so sibling ORDER round-trips (the editor hierarchy is
        // reorderable; pool order would shuffle siblings back to creation order).
        Array<EntityHandle> stack;
        for (EntityHandle r = scene.GetFirstRoot(); r.IsAssigned(); r = scene.GetNextSibling(r)) {
            stack.PushBack(r);
            while (!stack.IsEmpty()) {
                EntityHandle e = stack.Back();
                stack.PopBack();
                handles.PushBack(e);
                // Push children reversed so they POP in list order.
                Array<EntityHandle> kids;
                for (EntityHandle c = scene.GetFirstChild(e); c.IsAssigned(); c = scene.GetNextSibling(c)) {
                    kids.PushBack(c);
                }
                for (usize i = kids.Size(); i-- > 0;) { stack.PushBack(kids[i]); }
            }
        }
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
            draconic::core::Serialize(ar, "name", ename);
            draconic::core::Serialize(ar, "active", active);
            detail::SerializeGuid(ar, "parent", parentId);
            detail::SerializeTransform(ar, t);
        }
    } else {
        Array<Guid> ids;
        Array<Guid> parents;
        for (u32 i = 0; i < entityCount; ++i) {
            Guid id; String ename; u8 active = 0; Guid parentId; Transform t;
            detail::SerializeGuid(ar, "id", id);
            draconic::core::Serialize(ar, "name", ename);
            draconic::core::Serialize(ar, "active", active);
            detail::SerializeGuid(ar, "parent", parentId);
            detail::SerializeTransform(ar, t);
            // Corrupt-save recovery: a duplicate entity guid (the pre-fix RNG-collision bug)
            // gets a FRESH id so every entity stays uniquely addressable. Records addressed
            // to the shared guid (components, parent links) route to its FIRST holder.
            EntityHandle h;
            if (scene.FindEntity(id).IsAssigned())
            {
                DRACONIC_LOG_WARNING(u8"Scene",
                    u8"duplicate entity guid in save for '{}' - assigning a fresh id", ename);
                h = scene.CreateEntity(ename.AsView());
                ids.PushBack(scene.GetEntityId(h));   // parent RELINK by the fresh id
            }
            else
            {
                h = scene.CreateEntity(id, ename.AsView());
                ids.PushBack(id);
            }
            scene.SetActive(h, active != 0);
            scene.SetLocalTransform(h, t);
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
    // on `scene` (the normal case - managers are injected before load). Skipping an
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
            draconic::core::Serialize(ar, "type", typeId);
            r.manager->WriteComponent(ar, r.owner);
        }
    } else {
        for (u32 i = 0; i < componentCount; ++i) {
            Guid ownerId; String typeId;
            detail::SerializeGuid(ar, "owner", ownerId);
            draconic::core::Serialize(ar, "type", typeId);
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
// Post-load resolve pass (asset-pipeline design §8): bind every component's resource::Ref
// through the manager. Run after LoadScene once a ResourceManager over the cooked DB exists;
// idempotent (re-binding an already-bound ref is a cache hit).
inline void ResolveSceneResources(Scene& scene, draconic::resource::ResourceManager& resources) {
    scene.ForEachManager([&](ComponentManagerBase& manager) {
        manager.ResolveResources(resources);
    });
}

inline Status LoadScene(draconic::content::Instance& instance, Scene& scene) {
    UniquePtr<IStream> stream = instance.ReadData(u8"scene");
    if (stream.Get() == nullptr) { return Status{ ErrorCode::NotFound }; }
    BinarySerializer ser(*stream, SerializeMode::Read);
    SerializeScene(ser, scene);
    return Status{};
}

// A full-scene snapshot for the editor's Simulate loop (play-in-editor design: snapshot ->
// run -> restore). Capture serializes through the SAME path as .scene saves; Restore drains
// every entity in the target and deserializes back INTO THE SAME Scene instance, so borrowed
// references to the scene (pages, edit contexts, guid selections) stay valid - entity guids
// are part of the snapshot, so guid-keyed state re-resolves after restore.
class SceneSnapshot {
public:
    /// Serialize `scene` into a memory snapshot. Null on serializer failure.
    [[nodiscard]] static UniquePtr<SceneSnapshot> Capture(Scene& scene) {
        MemoryStream buffer;
        BinarySerializer ar(buffer, SerializeMode::Write);
        SerializeScene(ar, scene);
        if (!ar.IsOk()) { return UniquePtr<SceneSnapshot>{}; }
        UniquePtr<SceneSnapshot> snapshot = MakeUnique<SceneSnapshot>(DefaultAllocator());
        const Span<const byte> bytes = buffer.Bytes();
        snapshot->m_blob.Reserve(bytes.Size());
        for (byte b : bytes) { snapshot->m_blob.PushBack(b); }
        return snapshot;
    }

    /// Drain `scene` and rebuild it from the snapshot. Pass the resource manager to re-bind
    /// component refs immediately (the same post-load resolve pass scene loading runs).
    [[nodiscard]] Status Restore(Scene& scene,
                                 draconic::resource::ResourceManager* resources = nullptr) {
        // Drain: destroy every root (children go with them). Collect first - destroying
        // while iterating the entity storage is undefined.
        Array<EntityHandle> roots;
        scene.ForEachEntity([&](EntityHandle e) {
            if (!scene.GetParent(e).IsAssigned()) { roots.PushBack(e); }
        });
        for (EntityHandle root : roots) { scene.DestroyEntity(root); }

        MemoryStream buffer;
        (void)buffer.Write(m_blob.Data(), m_blob.Size());
        (void)buffer.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(buffer, SerializeMode::Read);
        SerializeScene(ar, scene);
        if (!ar.IsOk()) { return ar.GetStatus(); }
        if (resources != nullptr) { ResolveSceneResources(scene, *resources); }
        return Status{};
    }

private:
    Array<byte> m_blob;
};

DRACONIC_DEFINE_OBJECT(SceneDocument, "draconic::scene")

} // namespace draconic::scene
