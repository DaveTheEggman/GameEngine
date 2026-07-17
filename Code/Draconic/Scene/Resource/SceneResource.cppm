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
    // Scene-stream header (v2+): a magic sentinel no legacy stream can start with (a legacy
    // stream begins with the scene NAME's u32 length - always small), then the format
    // version. v2 adds length-prefixed component + system-settings records, so readers SKIP
    // unknown types instead of aborting (and template payloads can be sliced without a
    // live scene).
    constexpr u32 kSceneStreamMagic   = 0xD5C35CEEu;
    constexpr u32 kSceneStreamVersion = 2;

    // Peek the stream version. Leaves the stream positioned AFTER the header (v2+) or back
    // at the start (legacy v1 - no header).
    inline u32 ReadSceneStreamVersion(IStream& stream) {
        const i64 start = stream.Tell();
        u32 first = 0;
        if (stream.Read(&first, sizeof(first)) != sizeof(first) || first != kSceneStreamMagic) {
            (void)stream.Seek(start, SeekOrigin::Begin);
            return 1;
        }
        u32 version = 1;
        if (stream.Read(&version, sizeof(version)) != sizeof(version)) { return 1; }
        return version;
    }

    inline void WriteSceneStreamHeader(ISerializer& ar) {
        u32 magic = kSceneStreamMagic;
        u32 version = kSceneStreamVersion;
        draconic::core::Serialize(ar, "magic", magic);
        draconic::core::Serialize(ar, "version", version);
    }

    inline void SerializeTransform(ISerializer& ar, Transform& t) {
        draconic::core::Serialize(ar, "pos", t.position);
        draconic::core::Serialize(ar, "rot", t.rotation);
        draconic::core::Serialize(ar, "scl", t.scale);
    }
    // Pre-order subtree walk (parents before children, siblings in list order).
    inline void CollectSubtree(Scene& scene, EntityHandle root, Array<EntityHandle>& out) {
        Array<EntityHandle> stack;
        stack.PushBack(root);
        while (!stack.IsEmpty()) {
            EntityHandle e = stack.Back();
            stack.PopBack();
            out.PushBack(e);
            Array<EntityHandle> kids;
            for (EntityHandle c = scene.GetFirstChild(e); c.IsAssigned(); c = scene.GetNextSibling(c)) {
                kids.PushBack(c);
            }
            for (usize i = kids.Size(); i-- > 0;) { stack.PushBack(kids[i]); }
        }
    }

    // One component serialized to bytes (binary): baseline capture + save-time diffing.
    // Scene streams are always binary, so blob payloads replay through the same backend.
    inline void ComponentToBlob(ComponentManagerBase& manager, EntityHandle owner, Array<u8>& out) {
        MemoryStream buffer;
        BinarySerializer ar(buffer, SerializeMode::Write);
        manager.WriteComponent(ar, owner);
        out.Clear();
        const Span<const byte> bytes = buffer.Bytes();
        out.Reserve(bytes.Size());
        for (byte b : bytes) { out.PushBack(static_cast<u8>(b)); }
    }

    inline void ComponentFromBlob(ComponentManagerBase& manager, EntityHandle owner, Span<const u8> blob) {
        MemoryStream buffer;
        (void)buffer.Write(reinterpret_cast<const byte*>(blob.Data()), blob.Size());
        (void)buffer.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(buffer, SerializeMode::Read);
        manager.ReadComponent(ar, owner);
    }

    [[nodiscard]] inline bool BlobsEqual(Span<const u8> a, Span<const u8> b) {
        if (a.Size() != b.Size()) { return false; }
        for (usize i = 0; i < a.Size(); ++i) { if (a[i] != b[i]) { return false; } }
        return true;
    }

    [[nodiscard]] inline bool TransformsEqual(const Transform& a, const Transform& b) {
        return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z
            && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z
            && a.rotation.w == b.rotation.w
            && a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
    }
}

namespace detail {
    // Live-vs-baseline deltas for one instance, in the pending-descriptor shape a scene
    // load parks: SerializeScene's Referenced write serializes it, and prefab rebuild
    // (template changed) re-applies it onto a fresh respawn.
    [[nodiscard]] inline UniquePtr<Scene::PendingPrefabInstance>
    ComputeInstanceDeltas(Scene& scene, Scene::PrefabInstanceState& state) {
        auto pending = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
        pending->prefabId = state.prefabId;
        EntityHandle root = scene.FindEntity(state.rootEntityId);
        EntityHandle parent = root.IsAssigned() ? scene.GetParent(root) : EntityHandle::Invalid();
        pending->parentEntityId = parent.IsAssigned() ? scene.GetEntityId(parent) : Guid{};
        pending->rootTransform = root.IsAssigned() ? scene.GetLocalTransform(root) : Transform{};
        pending->sourceIds = state.sourceIds;
        pending->liveIds = state.liveIds;
        pending->rootLiveId = state.rootEntityId;
        pending->ownerRootEntityId = state.ownerRootEntityId;
        pending->nestedRootSourceId = state.nestedRootSourceId;

        for (usize i = 0; i < state.sourceIds.Size(); ++i) {
            EntityHandle live = scene.FindEntity(state.liveIds[i]);
            if (!live.IsAssigned()) { pending->destroyedMembers.PushBack(state.sourceIds[i]); continue; }
            if (live != root) {
                Transform t = scene.GetLocalTransform(live);
                if (!TransformsEqual(t, state.baselineTransforms[i])) {
                    pending->overrideTransformIds.PushBack(state.sourceIds[i]);
                    pending->overrideTransforms.PushBack(t);
                }
            }
            scene.ForEachManager([&](ComponentManagerBase& m) {
                if (!m.IsSerializable()) { return; }
                const Scene::PrefabComponentBaseline* baseline = nullptr;
                for (const Scene::PrefabComponentBaseline& b : state.componentBaselines) {
                    if (b.sourceEntity == state.sourceIds[i] && b.typeId.AsView() == m.SerializationTypeId()) {
                        baseline = &b;
                        break;
                    }
                }
                Scene::PendingPrefabComponentOp op;
                op.sourceEntity = state.sourceIds[i];
                op.typeId = String(m.SerializationTypeId());
                if (m.HasComponent(live)) {
                    Array<u8> blob;
                    ComponentToBlob(m, live, blob);
                    if (baseline == nullptr) {
                        op.op = 1u;   // add
                        op.blob = static_cast<Array<u8>&&>(blob);
                    } else if (!BlobsEqual(Span<const u8>{ blob.Data(), blob.Size() },
                                           Span<const u8>{ baseline->blob.Data(), baseline->blob.Size() })) {
                        op.op = 0u;   // modify
                        op.blob = static_cast<Array<u8>&&>(blob);
                    } else {
                        return;   // unchanged
                    }
                } else if (baseline != nullptr) {
                    op.op = 2u;   // remove
                } else {
                    return;   // never had it
                }
                pending->componentOps.PushBack(static_cast<Scene::PendingPrefabComponentOp&&>(op));
            });
        }
        return pending;
    }

    // Re-applies a pending descriptor's DELTAS onto a freshly spawned instance (`state` is
    // the spawn's registered state - its member map routes source ids to live entities).
    inline void ApplyPendingDeltas(Scene& scene, Scene::PrefabInstanceState* state,
                                   const Scene::PendingPrefabInstance& pending) {
        auto liveOf = [&](const Guid& sourceId) -> EntityHandle {
            if (state != nullptr) {
                for (usize i = 0; i < state->sourceIds.Size(); ++i) {
                    if (state->sourceIds[i] == sourceId) { return scene.FindEntity(state->liveIds[i]); }
                }
            }
            return EntityHandle::Invalid();
        };
        for (const Guid& dead : pending.destroyedMembers) {
            EntityHandle e = liveOf(dead);
            if (e.IsAssigned()) { scene.DestroyEntity(e); }
        }
        for (usize i = 0; i < pending.overrideTransformIds.Size() && i < pending.overrideTransforms.Size(); ++i) {
            EntityHandle e = liveOf(pending.overrideTransformIds[i]);
            if (e.IsAssigned()) { scene.SetLocalTransform(e, pending.overrideTransforms[i]); }
        }
        for (const Scene::PendingPrefabComponentOp& op : pending.componentOps) {
            EntityHandle e = liveOf(op.sourceEntity);
            ComponentManagerBase* manager = scene.FindManagerBySerializationId(op.typeId.AsView());
            if (!e.IsAssigned() || manager == nullptr) { continue; }
            if (op.op == 2u) {
                if (manager->HasComponent(e)) { manager->RemoveComponent(e); }
            } else {
                ComponentFromBlob(*manager, e, Span<const u8>{ op.blob.Data(), op.blob.Size() });
            }
        }
    }

    // One nested-instance record, serialized (the ref+delta shape shared by scene files'
    // prefab sections and prefab payloads' trailing records). `wireNested` gates the P4
    // link fields for pre-nesting saves.
    inline void WritePrefabRecord(ISerializer& ar, Scene::PendingPrefabInstance& d) {
        SerializeGuid(ar, "prefab", d.prefabId);
        SerializeGuid(ar, "parent", d.parentEntityId);
        SerializeTransform(ar, d.rootTransform);
        SerializeGuid(ar, "rootLive", d.rootLiveId);
        SerializeGuid(ar, "owner", d.ownerRootEntityId);
        SerializeGuid(ar, "nestedSrcRoot", d.nestedRootSourceId);

        u32 memberCount = static_cast<u32>(d.sourceIds.Size());
        ar.Key("members");
        ar.BeginArray(memberCount);
        for (u32 i = 0; i < memberCount; ++i) {
            SerializeGuid(ar, "src", d.sourceIds[i]);
            SerializeGuid(ar, "live", d.liveIds[i]);
        }
        ar.EndArray();

        u32 destroyedCount = static_cast<u32>(d.destroyedMembers.Size());
        ar.Key("destroyed");
        ar.BeginArray(destroyedCount);
        for (Guid& dead : d.destroyedMembers) { SerializeGuid(ar, "src", dead); }
        ar.EndArray();

        u32 transformCount = static_cast<u32>(d.overrideTransformIds.Size());
        ar.Key("transformOverrides");
        ar.BeginArray(transformCount);
        for (u32 i = 0; i < transformCount; ++i) {
            SerializeGuid(ar, "src", d.overrideTransformIds[i]);
            SerializeTransform(ar, d.overrideTransforms[i]);
        }
        ar.EndArray();

        u32 opCount = static_cast<u32>(d.componentOps.Size());
        ar.Key("componentOps");
        ar.BeginArray(opCount);
        for (Scene::PendingPrefabComponentOp& op : d.componentOps) {
            SerializeGuid(ar, "src", op.sourceEntity);
            draconic::core::Serialize(ar, "type", op.typeId);
            draconic::core::Serialize(ar, "op", op.op);
            draconic::core::Serialize(ar, "blob", op.blob);
        }
        ar.EndArray();
    }

    inline void ReadPrefabRecord(ISerializer& ar, Scene::PendingPrefabInstance& pending,
                                 bool wireNested) {
        SerializeGuid(ar, "prefab", pending.prefabId);
        SerializeGuid(ar, "parent", pending.parentEntityId);
        SerializeTransform(ar, pending.rootTransform);
        if (wireNested) {
            SerializeGuid(ar, "rootLive", pending.rootLiveId);
            SerializeGuid(ar, "owner", pending.ownerRootEntityId);
            SerializeGuid(ar, "nestedSrcRoot", pending.nestedRootSourceId);
        }

        u32 memberCount = 0;
        ar.Key("members");
        ar.BeginArray(memberCount);
        for (u32 i = 0; i < memberCount; ++i) {
            Guid src, live;
            SerializeGuid(ar, "src", src);
            SerializeGuid(ar, "live", live);
            pending.sourceIds.PushBack(src);
            pending.liveIds.PushBack(live);
        }
        ar.EndArray();

        u32 destroyedCount = 0;
        ar.Key("destroyed");
        ar.BeginArray(destroyedCount);
        for (u32 i = 0; i < destroyedCount; ++i) {
            Guid d;
            SerializeGuid(ar, "src", d);
            pending.destroyedMembers.PushBack(d);
        }
        ar.EndArray();

        u32 transformCount = 0;
        ar.Key("transformOverrides");
        ar.BeginArray(transformCount);
        for (u32 i = 0; i < transformCount; ++i) {
            Guid src; Transform t;
            SerializeGuid(ar, "src", src);
            SerializeTransform(ar, t);
            pending.overrideTransformIds.PushBack(src);
            pending.overrideTransforms.PushBack(t);
        }
        ar.EndArray();

        u32 opCount = 0;
        ar.Key("componentOps");
        ar.BeginArray(opCount);
        for (u32 i = 0; i < opCount; ++i) {
            Scene::PendingPrefabComponentOp op;
            SerializeGuid(ar, "src", op.sourceEntity);
            draconic::core::Serialize(ar, "type", op.typeId);
            draconic::core::Serialize(ar, "op", op.op);
            draconic::core::Serialize(ar, "blob", op.blob);
            pending.componentOps.PushBack(static_cast<Scene::PendingPrefabComponentOp&&>(op));
        }
        ar.EndArray();
    }

    // Instances whose ROOT lies inside `root`'s subtree (excluding `root`'s own instance):
    // captures turn these into nested records; their members leave the flat arrays.
    inline void CollectContainedInstances(Scene& scene, EntityHandle root,
                                          Array<Scene::PrefabInstanceState*>& outStates,
                                          HashMap<Guid, u8>& outMembers) {
        scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
            EntityHandle e = scene.FindEntity(s.rootEntityId);
            if (!e.IsAssigned() || e == root) { return; }
            bool inside = false;
            for (EntityHandle p = scene.GetParent(e); p.IsAssigned(); p = scene.GetParent(p)) {
                if (p == root) { inside = true; break; }
            }
            if (!inside) { return; }
            outStates.PushBack(&s);
            for (const Guid& live : s.liveIds) { outMembers.InsertOrAssign(live, 1u); }
        });
    }

    // Re-captures an instance's baselines from its CURRENT state. Nesting uses this after
    // applying an owner payload's record deltas: the owner's customization of a nested
    // instance becomes part of the BASELINE (so scene saves record only scene-level edits,
    // and owner-template changes propagate on rebuild instead of being pinned as overrides).
    inline void RecaptureBaselines(Scene& scene, Scene::PrefabInstanceState& state) {
        state.componentBaselines.Clear();
        for (usize i = 0; i < state.sourceIds.Size(); ++i) {
            EntityHandle live = scene.FindEntity(state.liveIds[i]);
            if (!live.IsAssigned()) { continue; }
            if (i < state.baselineTransforms.Size()) {
                state.baselineTransforms[i] = scene.GetLocalTransform(live);
            }
            scene.ForEachManager([&](ComponentManagerBase& m) {
                if (!m.IsSerializable() || !m.HasComponent(live)) { return; }
                Scene::PrefabComponentBaseline baseline;
                baseline.sourceEntity = state.sourceIds[i];
                baseline.typeId = String(m.SerializationTypeId());
                ComponentToBlob(m, live, baseline.blob);
                state.componentBaselines.PushBack(
                    static_cast<Scene::PrefabComponentBaseline&&>(baseline));
            });
        }
    }
}

// Bidirectional whole-scene serialization: scene name, the entity table + transform
// hierarchy, components, then SCENE-SYSTEM SETTINGS (environment/sky etc. - systems
// exposing SettingsType(), serialized under their own versioned payloads). On read,
// `scene` should be freshly created with its component managers + systems already present
// (so records route into their pools / settings blocks).
//
// `legacyProbe`: the settings section was appended AFTER the format shipped; streams saved
// before it simply END at the component array. Readers that have the underlying stream
// pass it here - at the settings boundary, exhausted stream = legacy save, settings keep
// their defaults (the next save upgrades). Null = the section is expected (fresh writes,
// snapshots). Write mode always writes it.
// How prefab instances persist in a scene stream:
//  - Referenced (scene files): instance members are EXCLUDED from the entity/component
//    arrays; a trailing section stores ref + deltas per instance. Loading parks pending
//    descriptors on the scene - run ResolveScenePrefabs afterwards to respawn them.
//  - Expanded (snapshots, e.g. play-in-editor): members serialize flat like every other
//    entity, plus the instance state (member maps + baselines) verbatim, so a restore
//    rebuilds exact prefab bookkeeping WITHOUT needing a payload resolver.
enum class ScenePrefabMode : u8 { Referenced = 0, Expanded = 1 };

// Wire values of the prefab-section mode tag. 0/1 = the P1..P3 formats (no nesting links);
// 2/3 = the same sections plus per-record nesting links (rootLive/owner/nestedSrcRoot).
// Writers emit 2/3; readers accept all four (older saves upgrade on the next write).
namespace detail {
    constexpr u8 kPrefabWireReferenced   = 0;
    constexpr u8 kPrefabWireExpanded     = 1;
    constexpr u8 kPrefabWireReferenced2  = 2;
    constexpr u8 kPrefabWireExpanded2    = 3;
}

// `includeSettings`: prefab payloads write an EMPTY system-settings section (a prefab is a
// subtree template, not a world - and SpawnPrefab must be able to walk PAST the section to
// reach the nested-instance records without applying settings to the target scene).
inline void SerializeScene(ISerializer& ar, Scene& scene, IStream* legacyProbe = nullptr,
                           ScenePrefabMode prefabMode = ScenePrefabMode::Referenced,
                           bool includeSettings = true) {
    const bool writing = ar.Mode() == SerializeMode::Write;

    // Stream version: writers emit the v2 header; readers sniff it THROUGH the serializer -
    // a legacy stream has no header, so the first u32 is the scene NAME's length (always
    // small, never the magic), whose characters are then consumed as a raw blob. Scene
    // streams are binary-only by design, which is what makes the sniff well-defined.
    u32 streamVersion = detail::kSceneStreamVersion;
    if (writing) {
        detail::WriteSceneStreamHeader(ar);
    }

    // Referenced writes exclude prefab-instance members from the plain entity/component
    // arrays (they respawn from their prefab at load; only deltas persist).
    HashMap<Guid, u8> prefabMembers;
    if (writing && prefabMode == ScenePrefabMode::Referenced) {
        scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
            for (const Guid& live : state.liveIds) { prefabMembers.InsertOrAssign(live, 1u); }
        });
    }

    // --- name (read side doubles as the version sniff - see above) ---
    String name = writing ? String(scene.Name()) : String{};
    if (writing) {
        draconic::core::Serialize(ar, "name", name);
    } else {
        u32 first = 0;
        draconic::core::Serialize(ar, "magic", first);
        if (first == detail::kSceneStreamMagic) {
            draconic::core::Serialize(ar, "version", streamVersion);
            draconic::core::Serialize(ar, "name", name);
        } else {
            streamVersion = 1;
            Array<u8> chars;
            chars.Resize(first);
            if (first > 0) { ar.Blob(chars.Data(), first); }
            name = String(StringView(reinterpret_cast<const utf8char*>(chars.Data()),
                                     static_cast<usize>(first)));
        }
        scene.SetName(name.AsView());
    }

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
                // Prefab-instance members persist as ref+deltas, never as plain records.
                // Their non-member children (user entities parented INTO an instance) still
                // serialize - their saved parent guid stays valid because instances respawn
                // with their SAVED member guids.
                if (prefabMembers.Find(scene.GetEntityId(e)) == nullptr) { handles.PushBack(e); }
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
            for (EntityHandle owner : m.OwnerHandles()) {
                if (prefabMembers.Find(scene.GetEntityId(owner)) != nullptr) { continue; }
                records.PushBack(Record{ &m, owner });
            }
        });
        componentCount = static_cast<u32>(records.Size());
    }
    ar.Key("components");
    ar.BeginArray(componentCount);
    if (writing) {
        // v2: records are length-prefixed BLOBS (the same bytes WriteComponent emits), so
        // readers can skip types this build doesn't know.
        for (Record& r : records) {
            Guid ownerId = scene.GetEntityId(r.owner);
            String typeId = String(r.manager->SerializationTypeId());
            detail::SerializeGuid(ar, "owner", ownerId);
            draconic::core::Serialize(ar, "type", typeId);
            Array<u8> blob;
            detail::ComponentToBlob(*r.manager, r.owner, blob);
            draconic::core::Serialize(ar, "data", blob);
        }
    } else {
        HashMap<String, u8> warned;
        for (u32 i = 0; i < componentCount; ++i) {
            Guid ownerId; String typeId;
            detail::SerializeGuid(ar, "owner", ownerId);
            draconic::core::Serialize(ar, "type", typeId);
            EntityHandle owner = scene.FindEntity(ownerId);
            ComponentManagerBase* manager = scene.FindManagerBySerializationId(typeId.AsView());
            if (streamVersion >= 2) {
                Array<u8> blob;
                draconic::core::Serialize(ar, "data", blob);
                if (owner.IsAssigned() && manager != nullptr) {
                    detail::ComponentFromBlob(*manager, owner,
                                              Span<const u8>{ blob.Data(), blob.Size() });
                } else if (manager == nullptr && warned.Find(typeId) == nullptr) {
                    warned.InsertOrAssign(typeId, 1u);
                    DRACONIC_LOG_WARNING(u8"Scene",
                        u8"skipping records of unknown component type '{}'", typeId);
                }
            } else {
                // Legacy inline records: not skippable - the manager must exist.
                if (owner.IsAssigned() && manager != nullptr) {
                    manager->ReadComponent(ar, owner);
                }
            }
        }
    }
    ar.EndArray();

    // --- scene-system settings (id, versioned payload) ---
    // NOTE: like components, reading a record requires its system to be present on `scene`
    // (systems are injected before load); an unknown id can't be skipped (length-prefixed
    // records are the same future robustness item as components).
    if (!writing && legacyProbe != nullptr && legacyProbe->Tell() >= legacyProbe->Size()) {
        return;   // pre-settings save: defaults stand, the next save upgrades the stream
    }
    u32 settingsCount = 0;
    if (writing && includeSettings) {
        scene.ForEachSystem([&](SceneSystem& s) { if (s.SettingsType() != nullptr) { ++settingsCount; } });
    }
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    if (writing) {
        // v2: each system's settings serialize into a length-prefixed blob - readers skip
        // systems this build doesn't have instead of aborting the section.
        scene.ForEachSystem([&](SceneSystem& s) {
            if (s.SettingsType() == nullptr || !includeSettings) { return; }
            String id(s.SettingsId());
            draconic::core::Serialize(ar, "system", id);
            MemoryStream buffer;
            {
                BinarySerializer sub(buffer, SerializeMode::Write);
                draconic::core::BeginVersionedPayload(sub, *s.SettingsType());
                sub.Key("settings"); sub.BeginObject();
                s.SerializeSettings(sub);
                sub.EndObject();
                draconic::core::EndVersionedPayload(sub);
            }
            Array<u8> blob;
            const Span<const byte> bytes = buffer.Bytes();
            blob.Reserve(bytes.Size());
            for (byte b : bytes) { blob.PushBack(static_cast<u8>(b)); }
            draconic::core::Serialize(ar, "data", blob);
        });
    } else {
        for (u32 i = 0; i < settingsCount; ++i) {
            String id;
            draconic::core::Serialize(ar, "system", id);
            SceneSystem* target = nullptr;
            scene.ForEachSystem([&](SceneSystem& s) {
                if (target == nullptr && s.SettingsType() != nullptr && s.SettingsId() == id.AsView()) {
                    target = &s;
                }
            });
            if (streamVersion >= 2) {
                Array<u8> blob;
                draconic::core::Serialize(ar, "data", blob);
                if (target == nullptr) {
                    DRACONIC_LOG_WARNING(u8"Scene",
                        u8"skipping settings of unknown system '{}'", id);
                    continue;
                }
                MemoryStream buffer;
                (void)buffer.Write(reinterpret_cast<const byte*>(blob.Data()), blob.Size());
                (void)buffer.Seek(0, SeekOrigin::Begin);
                BinarySerializer sub(buffer, SerializeMode::Read);
                draconic::core::BeginVersionedPayload(sub, *target->SettingsType());
                sub.Key("settings"); sub.BeginObject();
                target->SerializeSettings(sub);
                sub.EndObject();
                draconic::core::EndVersionedPayload(sub);
            } else {
                if (target == nullptr) {
                    DRACONIC_LOG_WARNING(u8"Scene",
                        u8"scene save carries settings for unknown system '{}' - rest of the section skipped", id);
                    break;   // legacy records aren't skippable; drop the remainder
                }
                draconic::core::BeginVersionedPayload(ar, *target->SettingsType());
                ar.Key("settings"); ar.BeginObject();
                target->SerializeSettings(ar);
                ar.EndObject();
                draconic::core::EndVersionedPayload(ar);
            }
        }
    }
    ar.EndArray();

    // --- prefab instances (appended after settings; older saves simply END here) ---
    if (!writing && legacyProbe != nullptr && legacyProbe->Tell() >= legacyProbe->Size()) {
        return;   // pre-prefab save: no instances to restore
    }
    u8 sectionMode = (prefabMode == ScenePrefabMode::Referenced)
        ? detail::kPrefabWireReferenced2 : detail::kPrefabWireExpanded2;
    draconic::core::Serialize(ar, "prefabMode", sectionMode);
    const bool wireNested = sectionMode == detail::kPrefabWireReferenced2
                         || sectionMode == detail::kPrefabWireExpanded2;
    const bool wireReferenced = sectionMode == detail::kPrefabWireReferenced
                             || sectionMode == detail::kPrefabWireReferenced2;

    if (wireReferenced) {
        // Ref + deltas: prefab id, placement, the SAVED member guid map (respawn preserves
        // entity identity across load), and overrides DERIVED right here by comparing live
        // state against the spawn-time baselines.
        u32 instanceCount = 0;
        if (writing) {
            scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState&) { ++instanceCount; });
        }
        ar.Key("prefabInstances");
        ar.BeginArray(instanceCount);
        if (writing) {
            scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
                // Overrides are DERIVED here: live state vs the spawn-time baselines.
                UniquePtr<Scene::PendingPrefabInstance> d = detail::ComputeInstanceDeltas(scene, state);
                detail::WritePrefabRecord(ar, *d);
            });
        } else {
            for (u32 n = 0; n < instanceCount; ++n) {
                auto pending = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
                detail::ReadPrefabRecord(ar, *pending, wireNested);
                scene.AddPendingPrefabInstance(static_cast<UniquePtr<Scene::PendingPrefabInstance>&&>(pending));
            }
        }
        ar.EndArray();
    } else {
        // Expanded (snapshots): members serialized flat above; persist the instance STATE
        // verbatim (member map + baselines) so restore rebuilds bookkeeping resolver-free.
        u32 instanceCount = 0;
        if (writing) {
            scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState&) { ++instanceCount; });
        }
        ar.Key("prefabStates");
        ar.BeginArray(instanceCount);
        if (writing) {
            scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
                detail::SerializeGuid(ar, "prefab", state.prefabId);
                detail::SerializeGuid(ar, "root", state.rootEntityId);
                detail::SerializeGuid(ar, "owner", state.ownerRootEntityId);
                detail::SerializeGuid(ar, "nestedSrcRoot", state.nestedRootSourceId);
                u32 memberCount = static_cast<u32>(state.sourceIds.Size());
                ar.Key("members");
                ar.BeginArray(memberCount);
                for (u32 i = 0; i < memberCount; ++i) {
                    detail::SerializeGuid(ar, "src", state.sourceIds[i]);
                    detail::SerializeGuid(ar, "live", state.liveIds[i]);
                    detail::SerializeTransform(ar, state.baselineTransforms[i]);
                }
                ar.EndArray();
                u32 baselineCount = static_cast<u32>(state.componentBaselines.Size());
                ar.Key("baselines");
                ar.BeginArray(baselineCount);
                for (Scene::PrefabComponentBaseline& b : state.componentBaselines) {
                    detail::SerializeGuid(ar, "src", b.sourceEntity);
                    draconic::core::Serialize(ar, "type", b.typeId);
                    draconic::core::Serialize(ar, "blob", b.blob);
                }
                ar.EndArray();
            });
        } else {
            for (u32 n = 0; n < instanceCount; ++n) {
                auto state = MakeUnique<Scene::PrefabInstanceState>(DefaultAllocator());
                detail::SerializeGuid(ar, "prefab", state->prefabId);
                detail::SerializeGuid(ar, "root", state->rootEntityId);
                if (wireNested) {
                    detail::SerializeGuid(ar, "owner", state->ownerRootEntityId);
                    detail::SerializeGuid(ar, "nestedSrcRoot", state->nestedRootSourceId);
                }
                u32 memberCount = 0;
                ar.Key("members");
                ar.BeginArray(memberCount);
                for (u32 i = 0; i < memberCount; ++i) {
                    Guid src, live; Transform t;
                    detail::SerializeGuid(ar, "src", src);
                    detail::SerializeGuid(ar, "live", live);
                    detail::SerializeTransform(ar, t);
                    state->sourceIds.PushBack(src);
                    state->liveIds.PushBack(live);
                    state->baselineTransforms.PushBack(t);
                }
                ar.EndArray();
                u32 baselineCount = 0;
                ar.Key("baselines");
                ar.BeginArray(baselineCount);
                for (u32 i = 0; i < baselineCount; ++i) {
                    Scene::PrefabComponentBaseline b;
                    detail::SerializeGuid(ar, "src", b.sourceEntity);
                    draconic::core::Serialize(ar, "type", b.typeId);
                    draconic::core::Serialize(ar, "blob", b.blob);
                    state->componentBaselines.PushBack(static_cast<Scene::PrefabComponentBaseline&&>(b));
                }
                ar.EndArray();
                scene.AddPrefabInstance(static_cast<UniquePtr<Scene::PrefabInstanceState>&&>(state));
            }
        }
        ar.EndArray();
    }
}

// Loads a cooked scene's "scene" data stream into `scene` (which must already have its
// component managers). Returns NotFound if the stream is missing.
// Post-load resolve pass (asset-pipeline design §8): bind every component's resource::Ref
// through the manager. Run after LoadScene once a ResourceManager over the cooked DB exists;
// idempotent (re-binding an already-bound ref is a cache hit).
inline void ResolveSceneResources(Scene& scene, draconic::resource::ResourceManager& resources) {
    // ALL systems, not just component managers: plain systems' settings blocks can hold
    // resource::Refs too (the environment's sky texture).
    scene.ForEachSystem([&](SceneSystem& system) {
        system.ResolveResources(resources);
    });
}


// ============================== Prefabs (P1: ref + deltas) ==================================
//
// A prefab is a content-DB instance whose "scene" data stream is a LoadScene-compatible
// capture of an entity subtree (PrefabDocument primary carries the name - a DISTINCT type so
// pages/creators/pickers tell prefabs from scenes, but the stream format is identical, so the
// scene editor page opens prefabs unchanged). Spawning instantiates the payload into a target
// scene with fresh (or preassigned) guids and records a PrefabInstanceState (member guid map +
// spawn-time BASELINES). Scenes persist instances as ref + deltas: overrides are DERIVED at
// save time by comparing live state against the baselines, so nothing tracks edits and
// undo/redo can never desynchronize the override set (docs/design/prefabs.md).

class PrefabDocument final : public ISerializable {
    DRACONIC_OBJECT(PrefabDocument, ISerializable)
public:
    String name;
    void Serialize(ISerializer& ar) override { draconic::core::Serialize(ar, "name", name); }
};


/// Captures `root`'s subtree into a LoadScene-compatible stream (entities + components + an
/// empty settings section + nested-instance records): the prefab PAYLOAD. Written with the
/// subtree's OWN guids - they become the stable sourceEntityIds that every instance's deltas
/// key on. Prefab instances INSIDE the subtree capture as nested RECORDS (ref + the
/// instance's current deltas), not flattened entities - selecting a group that contains
/// instances and making it a prefab preserves the links.
inline Status CapturePrefab(Scene& scene, EntityHandle root, IStream& out) {
    if (!root.IsAssigned()) { return Status{ ErrorCode::NotFound }; }
    BinarySerializer ar(out, SerializeMode::Write);
    detail::WriteSceneStreamHeader(ar);

    String name = String(scene.GetEntityName(root));
    draconic::core::Serialize(ar, "name", name);

    Array<Scene::PrefabInstanceState*> contained;
    HashMap<Guid, u8> nestedMembers;
    detail::CollectContainedInstances(scene, root, contained, nestedMembers);

    Array<EntityHandle> allHandles;
    detail::CollectSubtree(scene, root, allHandles);
    Array<EntityHandle> handles;
    for (EntityHandle e : allHandles) {
        if (nestedMembers.Find(scene.GetEntityId(e)) == nullptr) { handles.PushBack(e); }
    }

    ar.Key("entities");
    u32 entityCount = static_cast<u32>(handles.Size());
    ar.BeginArray(entityCount);
    for (EntityHandle e : handles) {
        Guid id = scene.GetEntityId(e);
        String ename = String(scene.GetEntityName(e));
        u8 active = scene.IsActive(e) ? 1u : 0u;
        // The subtree ROOT records a nil parent (spawn re-parents it at the target).
        Guid parentId = (e == root) ? Guid{} : scene.GetEntityId(scene.GetParent(e));
        Transform t = scene.GetLocalTransform(e);
        detail::SerializeGuid(ar, "id", id);
        draconic::core::Serialize(ar, "name", ename);
        draconic::core::Serialize(ar, "active", active);
        detail::SerializeGuid(ar, "parent", parentId);
        detail::SerializeTransform(ar, t);
    }
    ar.EndArray();

    struct Record { ComponentManagerBase* manager; EntityHandle owner; };
    Array<Record> records;
    scene.ForEachManager([&](ComponentManagerBase& m) {
        if (!m.IsSerializable()) { return; }
        for (EntityHandle owner : m.OwnerHandles()) {
            for (EntityHandle e : handles) {
                if (e == owner) { records.PushBack(Record{ &m, owner }); break; }
            }
        }
    });
    ar.Key("components");
    u32 componentCount = static_cast<u32>(records.Size());
    ar.BeginArray(componentCount);
    for (Record& r : records) {
        Guid ownerId = scene.GetEntityId(r.owner);
        String typeId = String(r.manager->SerializationTypeId());
        detail::SerializeGuid(ar, "owner", ownerId);
        draconic::core::Serialize(ar, "type", typeId);
        Array<u8> blob;
        detail::ComponentToBlob(*r.manager, r.owner, blob);
        draconic::core::Serialize(ar, "data", blob);
    }
    ar.EndArray();

    // Empty settings section: keeps the stream LoadScene-compatible (the prefab EDIT page
    // loads it like any scene; spawn walks PAST it to the nested records).
    u32 settingsCount = 0;
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    ar.EndArray();

    // Nested-instance records (P4): contained instances as ref + current deltas, in the
    // payload's namespace (live guids ARE the namespace ids here; a previously-nested
    // instance keeps its stable identity so existing spawns keep matching).
    u8 sectionMode = detail::kPrefabWireReferenced2;
    draconic::core::Serialize(ar, "prefabMode", sectionMode);
    u32 recordCount = static_cast<u32>(contained.Size());
    ar.Key("prefabInstances");
    ar.BeginArray(recordCount);
    for (Scene::PrefabInstanceState* state : contained) {
        UniquePtr<Scene::PendingPrefabInstance> d = detail::ComputeInstanceDeltas(scene, *state);
        if (!state->nestedRootSourceId.IsNil()) { d->rootLiveId = state->nestedRootSourceId; }
        d->ownerRootEntityId = Guid{};
        d->nestedRootSourceId = Guid{};
        detail::WritePrefabRecord(ar, *d);
    }
    ar.EndArray();
    return ar.IsOk() ? Status{} : ar.GetStatus();
}

/// Maps a prefab id to its payload stream (editor: source DB; player: cooked DB).
using PrefabPayloadResolver = Function<UniquePtr<IStream>(const Guid&)>;

/// Spawns a prefab payload into `scene`: creates every payload entity with a FRESH guid
/// (or the caller's preassigned one - scene loading preserves saved identities this way),
/// relinks parents inside the instance, parents payload roots under `parent` (invalid =
/// scene root), and registers a PrefabInstanceState with spawn-time baselines. Returns the
/// instance root (the FIRST payload root), or invalid on a malformed payload.
///
/// NESTING (P4): payloads written since nesting carry a trailing record section - the FLAT
/// FOREST of every instance the template contains at any depth (each relative to its OWN
/// template, so spawning never recurses and reference cycles cannot loop). With a `resolver`
/// each record's template spawns as a linked nested instance: the record's deltas (the
/// owner's customization) apply and then the baselines RE-capture, so scene saves record
/// only scene-level edits and owner-template changes propagate on rebuild. Without a
/// resolver the records are skipped with a warning (entities absent).
/// `nestedSceneDeltas` = the scene's saved sub-records for this instance's nested children
/// (matched by nestedRootSourceId == record rootLive): preserved guids + scene-level deltas.
inline EntityHandle SpawnPrefab(Scene& scene, IStream& payload, const Guid& prefabId,
                                EntityHandle parent = EntityHandle::Invalid(),
                                const HashMap<Guid, Guid>* preassigned = nullptr,
                                const PrefabPayloadResolver* resolver = nullptr,
                                const Array<const Scene::PendingPrefabInstance*>* nestedSceneDeltas = nullptr,
                                bool spawnNested = true) {
    const u32 streamVersion = detail::ReadSceneStreamVersion(payload);
    BinarySerializer ar(payload, SerializeMode::Read);

    String name;
    draconic::core::Serialize(ar, "name", name);

    auto state = MakeUnique<Scene::PrefabInstanceState>(DefaultAllocator());
    state->prefabId = prefabId;

    u32 entityCount = 0;
    ar.Key("entities");
    ar.BeginArray(entityCount);
    HashMap<Guid, Guid> liveBySource;
    Array<Guid> sourceParents;
    EntityHandle firstRoot = EntityHandle::Invalid();
    for (u32 i = 0; i < entityCount; ++i) {
        Guid sourceId; String ename; u8 active = 0; Guid sourceParent; Transform t;
        detail::SerializeGuid(ar, "id", sourceId);
        draconic::core::Serialize(ar, "name", ename);
        draconic::core::Serialize(ar, "active", active);
        detail::SerializeGuid(ar, "parent", sourceParent);
        detail::SerializeTransform(ar, t);

        EntityHandle live;
        const Guid* wanted = (preassigned != nullptr) ? preassigned->Find(sourceId) : nullptr;
        if (wanted != nullptr && !scene.FindEntity(*wanted).IsAssigned()) {
            live = scene.CreateEntity(*wanted, ename.AsView());
        } else {
            live = scene.CreateEntity(ename.AsView());   // fresh guid (collision-free mint)
        }
        scene.SetActive(live, active != 0);
        scene.SetLocalTransform(live, t);

        const Guid liveId = scene.GetEntityId(live);
        liveBySource.InsertOrAssign(sourceId, liveId);
        state->sourceIds.PushBack(sourceId);
        state->liveIds.PushBack(liveId);
        state->baselineTransforms.PushBack(t);
        sourceParents.PushBack(sourceParent);
        if (sourceParent == Guid{} && !firstRoot.IsAssigned()) { firstRoot = live; }
    }
    ar.EndArray();
    if (!ar.IsOk() || !firstRoot.IsAssigned()) { return EntityHandle::Invalid(); }

    // Relink: payload-internal parents through the map; the instance root under `parent`.
    // A prefab is SINGLE-rooted (capture, tinting, and apply-to-prefab all walk one root's
    // subtree); a legacy multi-root payload normalizes by parenting extra roots under the
    // first so nothing silently falls outside the instance.
    for (usize i = 0; i < state->sourceIds.Size(); ++i) {
        EntityHandle child = scene.FindEntity(state->liveIds[i]);
        if (!child.IsAssigned()) { continue; }
        if (sourceParents[i] == Guid{}) {
            if (child != firstRoot) { scene.SetParent(child, firstRoot); }
            else if (parent.IsAssigned()) { scene.SetParent(child, parent); }
        } else if (const Guid* liveParent = liveBySource.Find(sourceParents[i])) {
            EntityHandle p = scene.FindEntity(*liveParent);
            if (p.IsAssigned()) { scene.SetParent(child, p); }
        }
    }

    // Components: route to remapped owners, then capture each as a spawn-time baseline.
    // v2 payloads carry BLOB records: the blob applies to the live component AND becomes
    // the baseline directly (no re-serialize), and unknown types SKIP instead of failing.
    u32 componentCount = 0;
    ar.Key("components");
    ar.BeginArray(componentCount);
    for (u32 i = 0; i < componentCount; ++i) {
        Guid sourceOwner; String typeId;
        detail::SerializeGuid(ar, "owner", sourceOwner);
        draconic::core::Serialize(ar, "type", typeId);
        const Guid* liveId = liveBySource.Find(sourceOwner);
        EntityHandle owner = (liveId != nullptr) ? scene.FindEntity(*liveId) : EntityHandle::Invalid();
        ComponentManagerBase* manager = scene.FindManagerBySerializationId(typeId.AsView());
        if (streamVersion >= 2) {
            Array<u8> blob;
            draconic::core::Serialize(ar, "data", blob);
            if (!owner.IsAssigned() || manager == nullptr) {
                DRACONIC_LOG_WARNING(u8"Scene",
                    u8"prefab component record '{}' skipped (no owner/manager)", typeId);
                continue;
            }
            detail::ComponentFromBlob(*manager, owner, Span<const u8>{ blob.Data(), blob.Size() });
            // Baseline = RE-serialized from the live component, NOT the payload bytes: a
            // component data-version bump would otherwise read as a phantom override on
            // every instance (old-version blob != current-version blob for equal state).
            Scene::PrefabComponentBaseline baseline;
            baseline.sourceEntity = sourceOwner;
            baseline.typeId = typeId;
            detail::ComponentToBlob(*manager, owner, baseline.blob);
            state->componentBaselines.PushBack(static_cast<Scene::PrefabComponentBaseline&&>(baseline));
        } else {
            if (!owner.IsAssigned() || manager == nullptr) {
                DRACONIC_LOG_WARNING(u8"Scene", u8"prefab component record '{}' has no owner/manager - payload out of sync", typeId);
                return EntityHandle::Invalid();   // legacy records are not skippable
            }
            manager->ReadComponent(ar, owner);
            Scene::PrefabComponentBaseline baseline;
            baseline.sourceEntity = sourceOwner;
            baseline.typeId = typeId;
            detail::ComponentToBlob(*manager, owner, baseline.blob);
            state->componentBaselines.PushBack(static_cast<Scene::PrefabComponentBaseline&&>(baseline));
        }
    }
    ar.EndArray();
    if (!ar.IsOk()) { return EntityHandle::Invalid(); }

    const Guid rootGuid = scene.GetEntityId(firstRoot);
    state->rootEntityId = rootGuid;
    state->referencedPrefabIds.PushBack(prefabId);
    Scene::PrefabInstanceState* ownState = state.Get();
    scene.AddPrefabInstance(static_cast<UniquePtr<Scene::PrefabInstanceState>&&>(state));

    // ---- nested records (P4) ----
    // Reach the trailing section: new payloads write an EMPTY settings section; a NON-empty
    // one is a legacy Expanded save (members already spawned flat above - old behavior), and
    // a stream that simply ends here is a pre-nesting capture. Both skip cleanly.
    if (!spawnNested) { return firstRoot; }
    u32 settingsCount = 0;
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    ar.EndArray();
    if (!ar.IsOk() || settingsCount != 0) { return firstRoot; }
    if (payload.Tell() >= payload.Size()) { return firstRoot; }

    u8 sectionMode = 0;
    draconic::core::Serialize(ar, "prefabMode", sectionMode);
    if (sectionMode != detail::kPrefabWireReferenced2
        && sectionMode != detail::kPrefabWireReferenced) {
        return firstRoot;   // Expanded payload (legacy flatten): states already implicit
    }
    const bool wireNested = sectionMode == detail::kPrefabWireReferenced2;

    u32 recordCount = 0;
    ar.Key("prefabInstances");
    ar.BeginArray(recordCount);
    Array<UniquePtr<Scene::PendingPrefabInstance>> records;
    for (u32 n = 0; n < recordCount && ar.IsOk(); ++n) {
        auto record = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
        detail::ReadPrefabRecord(ar, *record, wireNested);
        records.PushBack(static_cast<UniquePtr<Scene::PendingPrefabInstance>&&>(record));
    }
    ar.EndArray();
    if (!ar.IsOk()) { return firstRoot; }

    // Owner-namespace id -> live guid: the payload's own entities, then each spawned
    // record's members (records were saved in registration order, so parents precede).
    HashMap<Guid, Guid> ownerNsToLive;
    for (const auto& kv : liveBySource) { ownerNsToLive.InsertOrAssign(kv.key, kv.value); }

    for (auto& recordPtr : records) {
        Scene::PendingPrefabInstance& r = *recordPtr;
        UniquePtr<IStream> childPayload = (resolver != nullptr && *resolver)
            ? (*resolver)(r.prefabId) : UniquePtr<IStream>{};
        if (childPayload.Get() == nullptr) {
            DRACONIC_LOG_WARNING(u8"Scene",
                u8"nested prefab record skipped - payload did not resolve (no resolver or missing asset)");
            continue;
        }

        // The scene's saved sub-record for THIS nested instance, if any.
        const Scene::PendingPrefabInstance* sub = nullptr;
        if (nestedSceneDeltas != nullptr) {
            for (const Scene::PendingPrefabInstance* candidate : *nestedSceneDeltas) {
                if (candidate != nullptr && candidate->nestedRootSourceId == r.rootLiveId) {
                    sub = candidate;
                    break;
                }
            }
        }

        HashMap<Guid, Guid> childPreassigned;
        if (sub != nullptr) {
            for (usize i = 0; i < sub->sourceIds.Size() && i < sub->liveIds.Size(); ++i) {
                childPreassigned.InsertOrAssign(sub->sourceIds[i], sub->liveIds[i]);
            }
        }

        EntityHandle recordParent = firstRoot;
        if (const Guid* liveParent = ownerNsToLive.Find(r.parentEntityId)) {
            EntityHandle p = scene.FindEntity(*liveParent);
            if (p.IsAssigned()) { recordParent = p; }
        }

        EntityHandle child = SpawnPrefab(scene, *childPayload, r.prefabId, recordParent,
                                         &childPreassigned, nullptr, nullptr,
                                         /*spawnNested=*/false);
        if (!child.IsAssigned()) { continue; }
        const Guid childRootGuid = scene.GetEntityId(child);
        Scene::PrefabInstanceState* childState = scene.FindPrefabInstanceByRoot(childRootGuid);
        if (childState == nullptr) { continue; }
        childState->ownerRootEntityId = rootGuid;
        childState->nestedRootSourceId = r.rootLiveId;

        // Owner customization -> then it BECOMES the baseline; scene-level deltas stay
        // overrides on top.
        scene.SetLocalTransform(child, r.rootTransform);
        detail::ApplyPendingDeltas(scene, childState, r);
        detail::RecaptureBaselines(scene, *childState);
        if (sub != nullptr) {
            if (sub->applyPlacement) {
                if (sub->parentEntityId != Guid{}) {
                    EntityHandle sceneParent = scene.FindEntity(sub->parentEntityId);
                    if (sceneParent.IsAssigned()) { scene.SetParent(child, sceneParent); }
                }
                scene.SetLocalTransform(child, sub->rootTransform);
            }
            detail::ApplyPendingDeltas(scene, childState, *sub);
        }

        ownState->referencedPrefabIds.PushBack(r.prefabId);
        for (usize i = 0; i < r.sourceIds.Size() && i < r.liveIds.Size(); ++i) {
            for (usize k = 0; k < childState->sourceIds.Size(); ++k) {
                if (childState->sourceIds[k] == r.sourceIds[i]) {
                    ownerNsToLive.InsertOrAssign(r.liveIds[i], childState->liveIds[k]);
                    break;
                }
            }
        }
    }
    return firstRoot;
}

/// The prefab member owning `entityId`, if any: the instance state + the member's index
/// (source id = state->sourceIds[index]). Inspector indicators and revert key on this.
struct PrefabMemberInfo {
    Scene::PrefabInstanceState* state = nullptr;
    usize memberIndex = 0;
};
[[nodiscard]] inline bool FindPrefabMember(Scene& scene, const Guid& entityId, PrefabMemberInfo& out) {
    bool found = false;
    scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
        if (found) { return; }
        for (usize i = 0; i < state.liveIds.Size(); ++i) {
            if (state.liveIds[i] == entityId) { out.state = &state; out.memberIndex = i; found = true; return; }
        }
    });
    return found;
}

[[nodiscard]] inline const Scene::PrefabComponentBaseline*
FindPrefabBaseline(const Scene::PrefabInstanceState& state, const Guid& sourceId, StringView typeId) {
    for (const Scene::PrefabComponentBaseline& b : state.componentBaselines) {
        if (b.sourceEntity == sourceId && b.typeId.AsView() == typeId) { return &b; }
    }
    return nullptr;
}

/// True when the member's component differs from its spawn-time baseline (an OVERRIDE):
/// modified bytes, added (no baseline), or removed (baseline without a live component).
[[nodiscard]] inline bool IsPrefabComponentOverridden(Scene& scene, const PrefabMemberInfo& member,
                                                      ComponentManagerBase& manager) {
    const Guid sourceId = member.state->sourceIds[member.memberIndex];
    const Scene::PrefabComponentBaseline* baseline =
        FindPrefabBaseline(*member.state, sourceId, manager.SerializationTypeId());
    EntityHandle live = scene.FindEntity(member.state->liveIds[member.memberIndex]);
    const bool has = live.IsAssigned() && manager.HasComponent(live);
    if (!has) { return baseline != nullptr; }
    if (baseline == nullptr) { return true; }
    Array<u8> blob;
    detail::ComponentToBlob(manager, live, blob);
    return !detail::BlobsEqual(Span<const u8>{ blob.Data(), blob.Size() },
                               Span<const u8>{ baseline->blob.Data(), baseline->blob.Size() });
}

/// Captures an INSTANCE's current state as a template payload (apply-to-prefab): records are
/// written with the members' SOURCE ids - other instances' deltas stay keyed correctly -
/// while entities the user ADDED under the instance keep their live guids as brand-new
/// source ids. The instance root records a nil parent, and ITS transform is written as the
/// template's root transform (an applied placement is not part of the template).
// Live-vs-PURE-TEMPLATE deltas for one instance: what apply-to-prefab writes into the OWNER
// payload's nested record (the instance's baselines have the owner customization folded in,
// so a plain baseline diff would lose it). v2 payloads carry length-prefixed component
// blobs, so the template baselines read STRAIGHT off the stream - no scene mutation. Legacy
// payloads fall back to the (degraded) baseline diff.
inline UniquePtr<Scene::PendingPrefabInstance> ComputeInstanceDeltasVsTemplate(
        Scene& scene, Scene::PrefabInstanceState& state, IStream& templatePayload) {
    const u32 streamVersion = detail::ReadSceneStreamVersion(templatePayload);
    if (streamVersion < 2) {
        DRACONIC_LOG_WARNING(u8"Scene",
            u8"apply-to-prefab: legacy nested template - owner customization may fold into the record");
        return detail::ComputeInstanceDeltas(scene, state);
    }
    BinarySerializer ar(templatePayload, SerializeMode::Read);

    String name;
    draconic::core::Serialize(ar, "name", name);

    HashMap<Guid, Transform> templateTransforms;
    u32 entityCount = 0;
    ar.Key("entities");
    ar.BeginArray(entityCount);
    for (u32 i = 0; i < entityCount; ++i) {
        Guid id; String ename; u8 active = 0; Guid parentId; Transform t;
        detail::SerializeGuid(ar, "id", id);
        draconic::core::Serialize(ar, "name", ename);
        draconic::core::Serialize(ar, "active", active);
        detail::SerializeGuid(ar, "parent", parentId);
        detail::SerializeTransform(ar, t);
        templateTransforms.InsertOrAssign(id, t);
    }
    ar.EndArray();

    struct TemplateBlob { Guid source; String typeId; Array<u8> blob; };
    Array<TemplateBlob> templateBlobs;
    u32 componentCount = 0;
    ar.Key("components");
    ar.BeginArray(componentCount);
    for (u32 i = 0; i < componentCount && ar.IsOk(); ++i) {
        TemplateBlob record;
        detail::SerializeGuid(ar, "owner", record.source);
        draconic::core::Serialize(ar, "type", record.typeId);
        draconic::core::Serialize(ar, "data", record.blob);
        templateBlobs.PushBack(static_cast<TemplateBlob&&>(record));
    }
    ar.EndArray();
    if (!ar.IsOk()) { return detail::ComputeInstanceDeltas(scene, state); }

    auto pending = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
    pending->prefabId = state.prefabId;
    EntityHandle liveRoot = scene.FindEntity(state.rootEntityId);
    EntityHandle parent = liveRoot.IsAssigned() ? scene.GetParent(liveRoot) : EntityHandle::Invalid();
    pending->parentEntityId = parent.IsAssigned() ? scene.GetEntityId(parent) : Guid{};
    pending->rootTransform = liveRoot.IsAssigned() ? scene.GetLocalTransform(liveRoot) : Transform{};
    pending->sourceIds = state.sourceIds;
    pending->liveIds = state.liveIds;
    pending->rootLiveId = state.rootEntityId;

    for (usize i = 0; i < state.sourceIds.Size(); ++i) {
        EntityHandle live = scene.FindEntity(state.liveIds[i]);
        if (!live.IsAssigned()) { pending->destroyedMembers.PushBack(state.sourceIds[i]); continue; }
        if (state.liveIds[i] != state.rootEntityId) {
            const Transform* baseline = templateTransforms.Find(state.sourceIds[i]);
            Transform t = scene.GetLocalTransform(live);
            if (baseline == nullptr || !detail::TransformsEqual(t, *baseline)) {
                pending->overrideTransformIds.PushBack(state.sourceIds[i]);
                pending->overrideTransforms.PushBack(t);
            }
        }
        scene.ForEachManager([&](ComponentManagerBase& m) {
            if (!m.IsSerializable()) { return; }
            const TemplateBlob* baseline = nullptr;
            for (const TemplateBlob& b : templateBlobs) {
                if (b.source == state.sourceIds[i] && b.typeId.AsView() == m.SerializationTypeId()) {
                    baseline = &b;
                    break;
                }
            }
            Scene::PendingPrefabComponentOp op;
            op.sourceEntity = state.sourceIds[i];
            op.typeId = String(m.SerializationTypeId());
            if (m.HasComponent(live)) {
                Array<u8> blob;
                detail::ComponentToBlob(m, live, blob);
                if (baseline == nullptr) {
                    op.op = 1u;
                    op.blob = static_cast<Array<u8>&&>(blob);
                } else if (!detail::BlobsEqual(Span<const u8>{ blob.Data(), blob.Size() },
                                               Span<const u8>{ baseline->blob.Data(), baseline->blob.Size() })) {
                    op.op = 0u;
                    op.blob = static_cast<Array<u8>&&>(blob);
                } else {
                    return;
                }
            } else if (baseline != nullptr) {
                op.op = 2u;
            } else {
                return;
            }
            pending->componentOps.PushBack(static_cast<Scene::PendingPrefabComponentOp&&>(op));
        });
    }
    return pending;
}

inline Status CaptureInstanceAsTemplate(Scene& scene, Scene::PrefabInstanceState& state, IStream& out,
                                        const PrefabPayloadResolver* resolver = nullptr) {
    EntityHandle root = scene.FindEntity(state.rootEntityId);
    if (!root.IsAssigned()) { return Status{ ErrorCode::NotFound }; }

    // live guid -> source id substitution for every surviving member.
    HashMap<Guid, Guid> sourceOf;
    for (usize i = 0; i < state.sourceIds.Size(); ++i) {
        sourceOf.InsertOrAssign(state.liveIds[i], state.sourceIds[i]);
    }
    auto substituted = [&](const Guid& live) -> Guid {
        const Guid* source = sourceOf.Find(live);
        return (source != nullptr) ? *source : live;   // user-added entities: live guid = new source id
    };

    BinarySerializer ar(out, SerializeMode::Write);
    detail::WriteSceneStreamHeader(ar);
    String name = String(scene.GetEntityName(root));
    draconic::core::Serialize(ar, "name", name);

    // Instances inside the subtree become nested RECORDS (owner-linked ones keep their
    // stable identity; a user-spawned instance inside gets ABSORBED as a new record).
    Array<Scene::PrefabInstanceState*> contained;
    HashMap<Guid, u8> nestedMembers;
    detail::CollectContainedInstances(scene, root, contained, nestedMembers);

    Array<EntityHandle> allHandles;
    detail::CollectSubtree(scene, root, allHandles);
    Array<EntityHandle> handles;
    for (EntityHandle e : allHandles) {
        if (nestedMembers.Find(scene.GetEntityId(e)) == nullptr) { handles.PushBack(e); }
    }

    // The root's live transform is this instance's PLACEMENT, not template content (root
    // transforms never propagate between instances - the Unity semantic); write the
    // spawn-time baseline (the template-authored root transform) instead, so an applied
    // placement never leaks into the asset.
    Transform rootTemplateTransform = scene.GetLocalTransform(root);
    for (usize i = 0; i < state.liveIds.Size(); ++i) {
        if (state.liveIds[i] == state.rootEntityId && i < state.baselineTransforms.Size()) {
            rootTemplateTransform = state.baselineTransforms[i];
            break;
        }
    }

    ar.Key("entities");
    u32 entityCount = static_cast<u32>(handles.Size());
    ar.BeginArray(entityCount);
    for (EntityHandle e : handles) {
        Guid id = substituted(scene.GetEntityId(e));
        String ename = String(scene.GetEntityName(e));
        u8 active = scene.IsActive(e) ? 1u : 0u;
        Guid parentId = (e == root) ? Guid{} : substituted(scene.GetEntityId(scene.GetParent(e)));
        Transform t = (e == root) ? rootTemplateTransform : scene.GetLocalTransform(e);
        detail::SerializeGuid(ar, "id", id);
        draconic::core::Serialize(ar, "name", ename);
        draconic::core::Serialize(ar, "active", active);
        detail::SerializeGuid(ar, "parent", parentId);
        detail::SerializeTransform(ar, t);
    }
    ar.EndArray();

    struct Record { ComponentManagerBase* manager; EntityHandle owner; };
    Array<Record> records;
    scene.ForEachManager([&](ComponentManagerBase& m) {
        if (!m.IsSerializable()) { return; }
        for (EntityHandle owner : m.OwnerHandles()) {
            for (EntityHandle e : handles) {
                if (e == owner) { records.PushBack(Record{ &m, owner }); break; }
            }
        }
    });
    ar.Key("components");
    u32 componentCount = static_cast<u32>(records.Size());
    ar.BeginArray(componentCount);
    for (Record& r : records) {
        Guid ownerId = substituted(scene.GetEntityId(r.owner));
        String typeId = String(r.manager->SerializationTypeId());
        detail::SerializeGuid(ar, "owner", ownerId);
        draconic::core::Serialize(ar, "type", typeId);
        Array<u8> blob;
        detail::ComponentToBlob(*r.manager, r.owner, blob);
        draconic::core::Serialize(ar, "data", blob);
    }
    ar.EndArray();

    u32 settingsCount = 0;
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    ar.EndArray();

    // Nested records: deltas vs the PURE child template when the resolver provides it (the
    // instance's baselines already absorbed this owner's customization - a baseline diff
    // would silently drop it); baseline diff is the degraded fallback.
    u8 sectionMode = detail::kPrefabWireReferenced2;
    draconic::core::Serialize(ar, "prefabMode", sectionMode);
    u32 recordCount = static_cast<u32>(contained.Size());
    ar.Key("prefabInstances");
    ar.BeginArray(recordCount);
    for (Scene::PrefabInstanceState* nested : contained) {
        UniquePtr<IStream> childPayload = (resolver != nullptr && *resolver)
            ? (*resolver)(nested->prefabId) : UniquePtr<IStream>{};
        UniquePtr<Scene::PendingPrefabInstance> d;
        if (childPayload.Get() != nullptr) {
            d = ComputeInstanceDeltasVsTemplate(scene, *nested, *childPayload);
        } else {
            DRACONIC_LOG_WARNING(u8"Scene",
                u8"apply-to-prefab: nested template unresolved - owner customization may be lost");
            d = detail::ComputeInstanceDeltas(scene, *nested);
        }
        if (!nested->nestedRootSourceId.IsNil()) { d->rootLiveId = nested->nestedRootSourceId; }
        d->parentEntityId = substituted(d->parentEntityId);
        d->ownerRootEntityId = Guid{};
        d->nestedRootSourceId = Guid{};
        detail::WritePrefabRecord(ar, *d);
    }
    ar.EndArray();
    return ar.IsOk() ? Status{} : ar.GetStatus();
}

/// Discards an instance's deltas: respawn from `payload` with the PRESERVED member guids and
/// placement (parent + root transform), applying nothing else. Returns false if the root is
/// gone or the payload fails to spawn.
inline bool RevertPrefabInstance(Scene& scene, const Guid& rootEntityId, Span<const byte> payload,
                                 const PrefabPayloadResolver* resolver = nullptr) {
    Scene::PrefabInstanceState* state = scene.FindPrefabInstanceByRoot(rootEntityId);
    if (state == nullptr) { return false; }
    EntityHandle root = scene.FindEntity(rootEntityId);
    if (!root.IsAssigned()) { return false; }
    const Guid prefabId = state->prefabId;
    EntityHandle parentHandle = scene.GetParent(root);
    const Guid parentId = parentHandle.IsAssigned() ? scene.GetEntityId(parentHandle) : Guid{};
    const Transform placement = scene.GetLocalTransform(root);
    HashMap<Guid, Guid> preassigned;
    for (usize i = 0; i < state->sourceIds.Size(); ++i) {
        preassigned.InsertOrAssign(state->sourceIds[i], state->liveIds[i]);
    }
    Array<Guid> liveIds = state->liveIds;

    // Nested instances revert with the owner: keep their member GUIDS (cross-references
    // stay valid) but none of their deltas, and let the template's placement win.
    Array<Scene::PrefabInstanceState*> contained;
    HashMap<Guid, u8> nestedMembers;
    detail::CollectContainedInstances(scene, root, contained, nestedMembers);
    Array<UniquePtr<Scene::PendingPrefabInstance>> subs;
    Array<Guid> subRoots;
    for (Scene::PrefabInstanceState* nested : contained) {
        auto sub = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
        sub->prefabId = nested->prefabId;
        sub->sourceIds = nested->sourceIds;
        sub->liveIds = nested->liveIds;
        sub->nestedRootSourceId = nested->nestedRootSourceId.IsNil()
            ? nested->rootEntityId : nested->nestedRootSourceId;
        sub->applyPlacement = false;
        subs.PushBack(static_cast<UniquePtr<Scene::PendingPrefabInstance>&&>(sub));
        subRoots.PushBack(nested->rootEntityId);
        for (const Guid& live : nested->liveIds) {
            EntityHandle e = scene.FindEntity(live);
            if (e.IsAssigned()) { scene.DestroyEntity(e); }
        }
    }
    for (const Guid& subRoot : subRoots) { scene.RemovePrefabInstance(subRoot); }

    for (const Guid& live : liveIds) {
        EntityHandle e = scene.FindEntity(live);
        if (e.IsAssigned()) { scene.DestroyEntity(e); }
    }
    scene.RemovePrefabInstance(rootEntityId);

    Array<const Scene::PendingPrefabInstance*> subPtrs;
    for (const auto& sub : subs) { subPtrs.PushBack(sub.Get()); }

    MemoryStream stream;
    (void)stream.Write(payload.Data(), payload.Size());
    (void)stream.Seek(0, SeekOrigin::Begin);
    EntityHandle parent = (parentId != Guid{}) ? scene.FindEntity(parentId) : EntityHandle::Invalid();
    EntityHandle spawned = SpawnPrefab(scene, stream, prefabId, parent, &preassigned,
                                       resolver, &subPtrs);
    if (!spawned.IsAssigned()) { return false; }
    scene.SetLocalTransform(spawned, placement);
    return true;
}

/// Resolves the PENDING prefab instances a scene load parked (SerializeScene reads the
/// ref+delta section but has no DB access): `resolver` maps a prefab id to its payload
/// stream (editor: source DB; runtime: cooked DB). Respawns each instance with its SAVED
/// member guids, re-applies the root placement and the deltas. Unresolvable prefabs are
/// skipped with a warning (their entities are simply absent).
inline void ResolveScenePrefabs(Scene& scene, const PrefabPayloadResolver& resolver) {
    Array<UniquePtr<Scene::PendingPrefabInstance>> pendings = scene.TakePendingPrefabInstances();

    // Nested records (owner set) don't spawn on their own - their OWNER's spawn consumes
    // them (preserved guids + scene-level deltas layered over the owner customization).
    for (auto& p : pendings) {
        if (!p->ownerRootEntityId.IsNil()) { continue; }

        UniquePtr<IStream> payload = resolver ? resolver(p->prefabId) : UniquePtr<IStream>{};
        if (payload.Get() == nullptr) {
            DRACONIC_LOG_WARNING(u8"Scene", u8"prefab instance skipped - payload for its prefab did not resolve");
            continue;
        }
        HashMap<Guid, Guid> preassigned;
        for (usize i = 0; i < p->sourceIds.Size() && i < p->liveIds.Size(); ++i) {
            preassigned.InsertOrAssign(p->sourceIds[i], p->liveIds[i]);
        }
        Array<const Scene::PendingPrefabInstance*> subPtrs;
        for (const auto& candidate : pendings) {
            if (candidate->ownerRootEntityId == p->rootLiveId && !candidate->ownerRootEntityId.IsNil()) {
                subPtrs.PushBack(candidate.Get());
            }
        }
        EntityHandle parent = (p->parentEntityId != Guid{}) ? scene.FindEntity(p->parentEntityId)
                                                            : EntityHandle::Invalid();
        EntityHandle root = SpawnPrefab(scene, *payload, p->prefabId, parent, &preassigned,
                                        &resolver, &subPtrs);
        if (!root.IsAssigned()) { continue; }
        scene.SetLocalTransform(root, p->rootTransform);
        detail::ApplyPendingDeltas(scene, scene.FindPrefabInstanceByRoot(scene.GetEntityId(root)), *p);
    }

    // Orphaned nested records (their owner record vanished): spawn standalone so the
    // entities aren't silently lost - they become plain top-level instances.
    for (auto& p : pendings) {
        if (p->ownerRootEntityId.IsNil()) { continue; }
        if (scene.FindEntity(p->rootLiveId).IsAssigned()) { continue; }   // owner spawned it
        UniquePtr<IStream> payload = resolver ? resolver(p->prefabId) : UniquePtr<IStream>{};
        if (payload.Get() == nullptr) { continue; }
        DRACONIC_LOG_WARNING(u8"Scene", u8"nested prefab record lost its owner - spawning standalone");
        HashMap<Guid, Guid> preassigned;
        for (usize i = 0; i < p->sourceIds.Size() && i < p->liveIds.Size(); ++i) {
            preassigned.InsertOrAssign(p->sourceIds[i], p->liveIds[i]);
        }
        EntityHandle parent = (p->parentEntityId != Guid{}) ? scene.FindEntity(p->parentEntityId)
                                                            : EntityHandle::Invalid();
        EntityHandle root = SpawnPrefab(scene, *payload, p->prefabId, parent, &preassigned,
                                        &resolver, nullptr);
        if (!root.IsAssigned()) { continue; }
        scene.SetLocalTransform(root, p->rootTransform);
        detail::ApplyPendingDeltas(scene, scene.FindPrefabInstanceByRoot(scene.GetEntityId(root)), *p);
    }
}

/// The template changed (its asset was saved / its product reloaded): rebuild every affected
/// TOP-LEVEL instance in `scene` from its template, preserving the user's deltas. An instance
/// is affected when it IS `prefabId` or its payload REFERENCES it (nested at any depth -
/// referencedPrefabIds, recorded at spawn); referencing instances rebuild from their OWN
/// template via `resolver`. Owned nested instances rebuild with their owner (scene-level
/// deltas + member guids preserved); a user-spawned instance INSIDE a rebuilt one respawns
/// standalone afterwards, and plain user entities parented under members are detached before
/// the teardown and re-attached after (they used to be silently destroyed). Returns the
/// number of instances rebuilt.
inline u32 RebuildPrefabInstances(Scene& scene, const Guid& prefabId, Span<const byte> payload,
                                  const PrefabPayloadResolver* resolver = nullptr) {
    struct RescuedChild { Guid child; Guid parentLiveId; };
    struct Item {
        UniquePtr<Scene::PendingPrefabInstance> own;
        Array<UniquePtr<Scene::PendingPrefabInstance>> subs;
        Array<Guid> subRoots;
        Array<RescuedChild> rescued;
        Guid root;
    };

    // Snapshot phase: nothing is destroyed until every affected instance's deltas (and its
    // nested instances') are captured.
    Array<Item> items;
    HashMap<Guid, u8> absorbedRoots;   // nested/contained roots handled via an owner item
    scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
        if (!state.ownerRootEntityId.IsNil()) { return; }   // rebuilds ride their owner
        bool affected = state.prefabId == prefabId;
        if (!affected) {
            for (const Guid& referenced : state.referencedPrefabIds) {
                if (referenced == prefabId) { affected = true; break; }
            }
        }
        if (!affected || absorbedRoots.Find(state.rootEntityId) != nullptr) { return; }
        EntityHandle root = scene.FindEntity(state.rootEntityId);
        if (!root.IsAssigned()) { return; }

        Item item;
        item.root = state.rootEntityId;
        item.own = detail::ComputeInstanceDeltas(scene, state);

        Array<Scene::PrefabInstanceState*> contained;
        HashMap<Guid, u8> nestedMembers;
        detail::CollectContainedInstances(scene, root, contained, nestedMembers);
        for (Scene::PrefabInstanceState* nested : contained) {
            auto sub = detail::ComputeInstanceDeltas(scene, *nested);
            sub->nestedRootSourceId = nested->nestedRootSourceId.IsNil()
                ? nested->rootEntityId : nested->nestedRootSourceId;
            item.subs.PushBack(static_cast<UniquePtr<Scene::PendingPrefabInstance>&&>(sub));
            item.subRoots.PushBack(nested->rootEntityId);
            absorbedRoots.InsertOrAssign(nested->rootEntityId, 1u);
        }

        // Plain user entities parented under members: detach now, re-attach post-respawn
        // (destroying a member destroys its whole subtree). Keyed by the member's LIVE
        // guid - preassignment preserves member guids through the respawn for OWN and
        // NESTED members alike, so one path rescues children of both. (The old
        // source-id mapping only covered the owner's members; a child under a nested
        // sub-instance member was skipped and died with the teardown.)
        HashMap<Guid, u8> allMembers;
        for (const Guid& live : state.liveIds) { allMembers.InsertOrAssign(live, 1u); }
        for (const auto& kv : nestedMembers) { allMembers.InsertOrAssign(kv.key, 1u); }
        for (const auto& kv : allMembers) {
            EntityHandle member = scene.FindEntity(kv.key);
            if (!member.IsAssigned()) { continue; }
            Array<EntityHandle> kids;
            for (EntityHandle c = scene.GetFirstChild(member); c.IsAssigned(); c = scene.GetNextSibling(c)) {
                kids.PushBack(c);
            }
            for (EntityHandle child : kids) {
                const Guid childGuid = scene.GetEntityId(child);
                if (allMembers.Find(childGuid) != nullptr) { continue; }
                item.rescued.PushBack(RescuedChild{ childGuid, kv.key });
                scene.SetParent(child, EntityHandle::Invalid(), true);
            }
        }
        items.PushBack(static_cast<Item&&>(item));
    });

    u32 rebuilt = 0;
    for (Item& item : items) {
        Scene::PendingPrefabInstance& p = *item.own;
        for (usize n = 0; n < item.subs.Size(); ++n) {
            for (const Guid& live : item.subs[n]->liveIds) {
                EntityHandle e = scene.FindEntity(live);
                if (e.IsAssigned()) { scene.DestroyEntity(e); }
            }
        }
        for (const Guid& subRoot : item.subRoots) { scene.RemovePrefabInstance(subRoot); }
        for (const Guid& live : p.liveIds) {
            EntityHandle e = scene.FindEntity(live);
            if (e.IsAssigned()) { scene.DestroyEntity(e); }
        }
        scene.RemovePrefabInstance(item.root);

        // This instance's template bytes: the changed payload when it IS the changed prefab,
        // else its own template via the resolver.
        MemoryStream stream;
        if (p.prefabId == prefabId) {
            (void)stream.Write(payload.Data(), payload.Size());
        } else {
            UniquePtr<IStream> own = (resolver != nullptr && *resolver)
                ? (*resolver)(p.prefabId) : UniquePtr<IStream>{};
            if (own.Get() == nullptr) {
                DRACONIC_LOG_WARNING(u8"Scene",
                    u8"instance referencing the changed prefab could not rebuild - its own template did not resolve");
                continue;
            }
            Array<byte> bytes;
            bytes.Resize(static_cast<usize>(own->Size()));
            (void)own->Read(bytes.Data(), bytes.Size());
            (void)stream.Write(bytes.Data(), bytes.Size());
        }
        (void)stream.Seek(0, SeekOrigin::Begin);

        HashMap<Guid, Guid> preassigned;
        for (usize i = 0; i < p.sourceIds.Size() && i < p.liveIds.Size(); ++i) {
            preassigned.InsertOrAssign(p.sourceIds[i], p.liveIds[i]);
        }
        Array<const Scene::PendingPrefabInstance*> subPtrs;
        for (const auto& sub : item.subs) { subPtrs.PushBack(sub.Get()); }

        EntityHandle parent = (p.parentEntityId != Guid{}) ? scene.FindEntity(p.parentEntityId)
                                                           : EntityHandle::Invalid();
        EntityHandle root = SpawnPrefab(scene, stream, p.prefabId, parent, &preassigned,
                                        resolver, &subPtrs);
        if (!root.IsAssigned()) { continue; }
        scene.SetLocalTransform(root, p.rootTransform);
        Scene::PrefabInstanceState* newState =
            scene.FindPrefabInstanceByRoot(scene.GetEntityId(root));
        detail::ApplyPendingDeltas(scene, newState, p);

        // Contained instances the template does NOT record (user-spawned inside): respawn
        // standalone so a template edit never eats user content.
        for (auto& sub : item.subs) {
            if (scene.FindEntity(sub->rootLiveId).IsAssigned()) { continue; }   // record consumed it
            UniquePtr<IStream> subPayload = (resolver != nullptr && *resolver)
                ? (*resolver)(sub->prefabId) : UniquePtr<IStream>{};
            if (subPayload.Get() == nullptr) { continue; }
            HashMap<Guid, Guid> subPreassigned;
            for (usize i = 0; i < sub->sourceIds.Size() && i < sub->liveIds.Size(); ++i) {
                subPreassigned.InsertOrAssign(sub->sourceIds[i], sub->liveIds[i]);
            }
            EntityHandle subParent = (sub->parentEntityId != Guid{})
                ? scene.FindEntity(sub->parentEntityId) : EntityHandle::Invalid();
            EntityHandle subRoot = SpawnPrefab(scene, *subPayload, sub->prefabId, subParent,
                                               &subPreassigned, resolver, nullptr);
            if (!subRoot.IsAssigned()) { continue; }
            scene.SetLocalTransform(subRoot, sub->rootTransform);
            detail::ApplyPendingDeltas(scene,
                scene.FindPrefabInstanceByRoot(scene.GetEntityId(subRoot)), *sub);
        }

        // Re-attach rescued user children: the member guid survived the respawn
        // (preassigned), so a plain lookup finds the new parent. A member the template
        // no longer has falls back to the instance root - the child stays with the
        // instance instead of being orphaned at scene root (or destroyed, pre-fix).
        for (const RescuedChild& rescue : item.rescued) {
            EntityHandle child = scene.FindEntity(rescue.child);
            if (!child.IsAssigned()) { continue; }
            EntityHandle member = scene.FindEntity(rescue.parentLiveId);
            scene.SetParent(child, member.IsAssigned() ? member : root, true);
        }
        ++rebuilt;
    }
    return rebuilt;
}

inline Status LoadScene(draconic::content::Instance& instance, Scene& scene) {
    UniquePtr<IStream> stream = instance.ReadData(u8"scene");
    if (stream.Get() == nullptr) { return Status{ ErrorCode::NotFound }; }
    BinarySerializer ser(*stream, SerializeMode::Read);
    SerializeScene(ser, scene, stream.Get());   // probe: pre-settings saves end at components
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
        // Expanded: members serialize flat + instance state verbatim, so Restore needs no
        // prefab payload resolver (snapshots must be self-contained).
        SerializeScene(ar, scene, nullptr, ScenePrefabMode::Expanded);
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
        scene.ClearPrefabInstances();   // the snapshot's Expanded section repopulates state

        MemoryStream buffer;
        (void)buffer.Write(m_blob.Data(), m_blob.Size());
        (void)buffer.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(buffer, SerializeMode::Read);
        SerializeScene(ar, scene, nullptr, ScenePrefabMode::Expanded);
        if (!ar.IsOk()) { return ar.GetStatus(); }
        if (resources != nullptr) { ResolveSceneResources(scene, *resources); }
        return Status{};
    }

private:
    Array<byte> m_blob;
};

DRACONIC_DEFINE_OBJECT(SceneDocument, "draconic::scene")
DRACONIC_DEFINE_OBJECT(PrefabDocument, "draconic::scene")

} // namespace draconic::scene
