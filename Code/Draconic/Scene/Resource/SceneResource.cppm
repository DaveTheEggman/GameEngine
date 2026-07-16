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

inline void SerializeScene(ISerializer& ar, Scene& scene, IStream* legacyProbe = nullptr,
                           ScenePrefabMode prefabMode = ScenePrefabMode::Referenced) {
    const bool writing = ar.Mode() == SerializeMode::Write;

    // Referenced writes exclude prefab-instance members from the plain entity/component
    // arrays (they respawn from their prefab at load; only deltas persist).
    HashMap<Guid, u8> prefabMembers;
    if (writing && prefabMode == ScenePrefabMode::Referenced) {
        scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
            for (const Guid& live : state.liveIds) { prefabMembers.InsertOrAssign(live, 1u); }
        });
    }

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

    // --- scene-system settings (id, versioned payload) ---
    // NOTE: like components, reading a record requires its system to be present on `scene`
    // (systems are injected before load); an unknown id can't be skipped (length-prefixed
    // records are the same future robustness item as components).
    if (!writing && legacyProbe != nullptr && legacyProbe->Tell() >= legacyProbe->Size()) {
        return;   // pre-settings save: defaults stand, the next save upgrades the stream
    }
    u32 settingsCount = 0;
    if (writing) {
        scene.ForEachSystem([&](SceneSystem& s) { if (s.SettingsType() != nullptr) { ++settingsCount; } });
    }
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    if (writing) {
        scene.ForEachSystem([&](SceneSystem& s) {
            if (s.SettingsType() == nullptr) { return; }
            String id(s.SettingsId());
            draconic::core::Serialize(ar, "system", id);
            draconic::core::BeginVersionedPayload(ar, *s.SettingsType());
            ar.Key("settings"); ar.BeginObject();
            s.SerializeSettings(ar);
            ar.EndObject();
            draconic::core::EndVersionedPayload(ar);
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
            if (target == nullptr) {
                DRACONIC_LOG_WARNING(u8"Scene",
                    u8"scene save carries settings for unknown system '{}' - rest of the section skipped", id);
                break;   // binary records aren't skippable; drop the remainder (defaults stand)
            }
            draconic::core::BeginVersionedPayload(ar, *target->SettingsType());
            ar.Key("settings"); ar.BeginObject();
            target->SerializeSettings(ar);
            ar.EndObject();
            draconic::core::EndVersionedPayload(ar);
        }
    }
    ar.EndArray();

    // --- prefab instances (appended after settings; older saves simply END here) ---
    if (!writing && legacyProbe != nullptr && legacyProbe->Tell() >= legacyProbe->Size()) {
        return;   // pre-prefab save: no instances to restore
    }
    u8 sectionMode = static_cast<u8>(prefabMode);
    draconic::core::Serialize(ar, "prefabMode", sectionMode);

    if (static_cast<ScenePrefabMode>(sectionMode) == ScenePrefabMode::Referenced) {
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
                detail::SerializeGuid(ar, "prefab", d->prefabId);
                detail::SerializeGuid(ar, "parent", d->parentEntityId);
                detail::SerializeTransform(ar, d->rootTransform);

                u32 memberCount = static_cast<u32>(d->sourceIds.Size());
                ar.Key("members");
                ar.BeginArray(memberCount);
                for (u32 i = 0; i < memberCount; ++i) {
                    detail::SerializeGuid(ar, "src", d->sourceIds[i]);
                    detail::SerializeGuid(ar, "live", d->liveIds[i]);
                }
                ar.EndArray();

                u32 destroyedCount = static_cast<u32>(d->destroyedMembers.Size());
                ar.Key("destroyed");
                ar.BeginArray(destroyedCount);
                for (Guid& dead : d->destroyedMembers) { detail::SerializeGuid(ar, "src", dead); }
                ar.EndArray();

                u32 transformCount = static_cast<u32>(d->overrideTransformIds.Size());
                ar.Key("transformOverrides");
                ar.BeginArray(transformCount);
                for (u32 i = 0; i < transformCount; ++i) {
                    detail::SerializeGuid(ar, "src", d->overrideTransformIds[i]);
                    detail::SerializeTransform(ar, d->overrideTransforms[i]);
                }
                ar.EndArray();

                u32 opCount = static_cast<u32>(d->componentOps.Size());
                ar.Key("componentOps");
                ar.BeginArray(opCount);
                for (Scene::PendingPrefabComponentOp& op : d->componentOps) {
                    detail::SerializeGuid(ar, "src", op.sourceEntity);
                    draconic::core::Serialize(ar, "type", op.typeId);
                    draconic::core::Serialize(ar, "op", op.op);
                    draconic::core::Serialize(ar, "blob", op.blob);
                }
                ar.EndArray();
            });
        } else {
            for (u32 n = 0; n < instanceCount; ++n) {
                auto pending = MakeUnique<Scene::PendingPrefabInstance>(DefaultAllocator());
                detail::SerializeGuid(ar, "prefab", pending->prefabId);
                detail::SerializeGuid(ar, "parent", pending->parentEntityId);
                detail::SerializeTransform(ar, pending->rootTransform);

                u32 memberCount = 0;
                ar.Key("members");
                ar.BeginArray(memberCount);
                for (u32 i = 0; i < memberCount; ++i) {
                    Guid src, live;
                    detail::SerializeGuid(ar, "src", src);
                    detail::SerializeGuid(ar, "live", live);
                    pending->sourceIds.PushBack(src);
                    pending->liveIds.PushBack(live);
                }
                ar.EndArray();

                u32 destroyedCount = 0;
                ar.Key("destroyed");
                ar.BeginArray(destroyedCount);
                for (u32 i = 0; i < destroyedCount; ++i) {
                    Guid d;
                    detail::SerializeGuid(ar, "src", d);
                    pending->destroyedMembers.PushBack(d);
                }
                ar.EndArray();

                u32 transformCount = 0;
                ar.Key("transformOverrides");
                ar.BeginArray(transformCount);
                for (u32 i = 0; i < transformCount; ++i) {
                    Guid src; Transform t;
                    detail::SerializeGuid(ar, "src", src);
                    detail::SerializeTransform(ar, t);
                    pending->overrideTransformIds.PushBack(src);
                    pending->overrideTransforms.PushBack(t);
                }
                ar.EndArray();

                u32 opCount = 0;
                ar.Key("componentOps");
                ar.BeginArray(opCount);
                for (u32 i = 0; i < opCount; ++i) {
                    Scene::PendingPrefabComponentOp op;
                    detail::SerializeGuid(ar, "src", op.sourceEntity);
                    draconic::core::Serialize(ar, "type", op.typeId);
                    draconic::core::Serialize(ar, "op", op.op);
                    draconic::core::Serialize(ar, "blob", op.blob);
                    pending->componentOps.PushBack(static_cast<Scene::PendingPrefabComponentOp&&>(op));
                }
                ar.EndArray();

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
/// empty settings section): the prefab PAYLOAD. Written with the subtree's OWN guids - they
/// become the stable sourceEntityIds that every instance's deltas key on.
inline Status CapturePrefab(Scene& scene, EntityHandle root, IStream& out) {
    if (!root.IsAssigned()) { return Status{ ErrorCode::NotFound }; }
    BinarySerializer ar(out, SerializeMode::Write);

    String name = String(scene.GetEntityName(root));
    draconic::core::Serialize(ar, "name", name);

    Array<EntityHandle> handles;
    detail::CollectSubtree(scene, root, handles);

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
        r.manager->WriteComponent(ar, r.owner);
    }
    ar.EndArray();

    // Empty settings section: keeps the stream LoadScene-compatible (the prefab EDIT page
    // loads it like any scene; spawn stops reading before this point).
    u32 settingsCount = 0;
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    ar.EndArray();
    return ar.IsOk() ? Status{} : ar.GetStatus();
}

/// Spawns a prefab payload into `scene`: creates every payload entity with a FRESH guid
/// (or the caller's preassigned one - scene loading preserves saved identities this way),
/// relinks parents inside the instance, parents payload roots under `parent` (invalid =
/// scene root), and registers a PrefabInstanceState with spawn-time baselines. Returns the
/// instance root (the FIRST payload root), or invalid on a malformed payload.
inline EntityHandle SpawnPrefab(Scene& scene, IStream& payload, const Guid& prefabId,
                                EntityHandle parent = EntityHandle::Invalid(),
                                const HashMap<Guid, Guid>* preassigned = nullptr) {
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
        if (!owner.IsAssigned() || manager == nullptr) {
            DRACONIC_LOG_WARNING(u8"Scene", u8"prefab component record '{}' has no owner/manager - payload out of sync", typeId);
            return EntityHandle::Invalid();   // binary records are not skippable
        }
        manager->ReadComponent(ar, owner);
        Scene::PrefabComponentBaseline baseline;
        baseline.sourceEntity = sourceOwner;
        baseline.typeId = typeId;
        detail::ComponentToBlob(*manager, owner, baseline.blob);
        state->componentBaselines.PushBack(static_cast<Scene::PrefabComponentBaseline&&>(baseline));
    }
    ar.EndArray();
    if (!ar.IsOk()) { return EntityHandle::Invalid(); }
    // (the trailing settings section is deliberately not read - spawn needs none of it)

    state->rootEntityId = scene.GetEntityId(firstRoot);
    scene.AddPrefabInstance(static_cast<UniquePtr<Scene::PrefabInstanceState>&&>(state));
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
inline Status CaptureInstanceAsTemplate(Scene& scene, Scene::PrefabInstanceState& state, IStream& out) {
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
    String name = String(scene.GetEntityName(root));
    draconic::core::Serialize(ar, "name", name);

    Array<EntityHandle> handles;
    detail::CollectSubtree(scene, root, handles);

    ar.Key("entities");
    u32 entityCount = static_cast<u32>(handles.Size());
    ar.BeginArray(entityCount);
    for (EntityHandle e : handles) {
        Guid id = substituted(scene.GetEntityId(e));
        String ename = String(scene.GetEntityName(e));
        u8 active = scene.IsActive(e) ? 1u : 0u;
        Guid parentId = (e == root) ? Guid{} : substituted(scene.GetEntityId(scene.GetParent(e)));
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
        Guid ownerId = substituted(scene.GetEntityId(r.owner));
        String typeId = String(r.manager->SerializationTypeId());
        detail::SerializeGuid(ar, "owner", ownerId);
        draconic::core::Serialize(ar, "type", typeId);
        r.manager->WriteComponent(ar, r.owner);
    }
    ar.EndArray();

    u32 settingsCount = 0;
    ar.Key("systemSettings");
    ar.BeginArray(settingsCount);
    ar.EndArray();
    return ar.IsOk() ? Status{} : ar.GetStatus();
}

/// Discards an instance's deltas: respawn from `payload` with the PRESERVED member guids and
/// placement (parent + root transform), applying nothing else. Returns false if the root is
/// gone or the payload fails to spawn.
inline bool RevertPrefabInstance(Scene& scene, const Guid& rootEntityId, Span<const byte> payload) {
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

    for (const Guid& live : liveIds) {
        EntityHandle e = scene.FindEntity(live);
        if (e.IsAssigned()) { scene.DestroyEntity(e); }
    }
    scene.RemovePrefabInstance(rootEntityId);

    MemoryStream stream;
    (void)stream.Write(payload.Data(), payload.Size());
    (void)stream.Seek(0, SeekOrigin::Begin);
    EntityHandle parent = (parentId != Guid{}) ? scene.FindEntity(parentId) : EntityHandle::Invalid();
    EntityHandle spawned = SpawnPrefab(scene, stream, prefabId, parent, &preassigned);
    if (!spawned.IsAssigned()) { return false; }
    scene.SetLocalTransform(spawned, placement);
    return true;
}

/// Resolves the PENDING prefab instances a scene load parked (SerializeScene reads the
/// ref+delta section but has no DB access): `resolver` maps a prefab id to its payload
/// stream (editor: source DB; runtime: cooked DB). Respawns each instance with its SAVED
/// member guids, re-applies the root placement and the deltas. Unresolvable prefabs are
/// skipped with a warning (their entities are simply absent).
inline void ResolveScenePrefabs(Scene& scene,
                                const Function<UniquePtr<IStream>(const Guid&)>& resolver) {
    Array<UniquePtr<Scene::PendingPrefabInstance>> pendings = scene.TakePendingPrefabInstances();
    for (auto& p : pendings) {
        UniquePtr<IStream> payload = resolver ? resolver(p->prefabId) : UniquePtr<IStream>{};
        if (payload.Get() == nullptr) {
            DRACONIC_LOG_WARNING(u8"Scene", u8"prefab instance skipped - payload for its prefab did not resolve");
            continue;
        }
        HashMap<Guid, Guid> preassigned;
        for (usize i = 0; i < p->sourceIds.Size() && i < p->liveIds.Size(); ++i) {
            preassigned.InsertOrAssign(p->sourceIds[i], p->liveIds[i]);
        }
        EntityHandle parent = (p->parentEntityId != Guid{}) ? scene.FindEntity(p->parentEntityId)
                                                            : EntityHandle::Invalid();
        EntityHandle root = SpawnPrefab(scene, *payload, p->prefabId, parent, &preassigned);
        if (!root.IsAssigned()) { continue; }
        scene.SetLocalTransform(root, p->rootTransform);
        detail::ApplyPendingDeltas(scene, scene.FindPrefabInstanceByRoot(scene.GetEntityId(root)), *p);
    }
}

/// The template changed (its asset was saved / its product reloaded): rebuild every instance
/// of `prefabId` in `scene` from the NEW payload, preserving the user's deltas - compute the
/// current overrides, destroy the members, respawn with the PRESERVED member guids (members
/// the new template dropped simply don't respawn; new template members spawn fresh), and
/// re-apply the deltas. Returns the number of instances rebuilt.
inline u32 RebuildPrefabInstances(Scene& scene, const Guid& prefabId, Span<const byte> payload) {
    // Snapshot the pendings FIRST (destroying entities mutates the state list).
    Array<UniquePtr<Scene::PendingPrefabInstance>> pendings;
    Array<Guid> roots;
    scene.ForEachPrefabInstance([&](Scene::PrefabInstanceState& state) {
        if (state.prefabId != prefabId) { return; }
        pendings.PushBack(detail::ComputeInstanceDeltas(scene, state));
        roots.PushBack(state.rootEntityId);
    });

    u32 rebuilt = 0;
    for (usize n = 0; n < pendings.Size(); ++n) {
        Scene::PendingPrefabInstance& p = *pendings[n];
        // Tear down the old instance: every still-live member (destroying a parent takes its
        // subtree; FindEntity guards the rest), then the bookkeeping.
        for (const Guid& live : p.liveIds) {
            EntityHandle e = scene.FindEntity(live);
            if (e.IsAssigned()) { scene.DestroyEntity(e); }
        }
        scene.RemovePrefabInstance(roots[n]);

        MemoryStream stream;
        (void)stream.Write(payload.Data(), payload.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        HashMap<Guid, Guid> preassigned;
        for (usize i = 0; i < p.sourceIds.Size() && i < p.liveIds.Size(); ++i) {
            preassigned.InsertOrAssign(p.sourceIds[i], p.liveIds[i]);
        }
        EntityHandle parent = (p.parentEntityId != Guid{}) ? scene.FindEntity(p.parentEntityId)
                                                           : EntityHandle::Invalid();
        EntityHandle root = SpawnPrefab(scene, stream, prefabId, parent, &preassigned);
        if (!root.IsAssigned()) { continue; }
        scene.SetLocalTransform(root, p.rootTransform);
        detail::ApplyPendingDeltas(scene, scene.FindPrefabInstanceByRoot(scene.GetEntityId(root)), p);
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
