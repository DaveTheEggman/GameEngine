/// Raptor::Scene — the `:scene` partition.
///
/// Scene: an isolated world of entities (and, in later phases, a transform hierarchy
/// + per-scene systems). This phase is the entity table: a pooled, generation-guarded
/// entity store with a free-list for slot reuse and a persistent-Guid <-> handle map
/// (so references survive save/load). Entities carry an active flag and an optional
/// name. Destruction is immediate for now; deferred-during-update lands with the
/// update loop in a later phase.

module;
#include "Core/Prelude.h"

export module raptor.scene:scene;

import raptor.core;
import :entity;

using namespace raptor::core;

export namespace raptor::scene {

class Scene {
public:
    explicit Scene(StringView name = {}, IAllocator& allocator = DefaultAllocator())
        : m_allocator(&allocator), m_name(name, allocator) {}

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] StringView Name() const noexcept { return m_name.AsView(); }
    void SetName(StringView name) { m_name = String(name); }

    [[nodiscard]] u32 EntityCount() const noexcept { return m_aliveCount; }

    // Creates an entity with a fresh random Guid.
    EntityHandle CreateEntity(StringView name = {}) {
        return CreateEntityInternal(Guid::Generate(m_rng), name);
    }

    // Creates an entity with a specific Guid (used when loading a scene from disk).
    EntityHandle CreateEntity(const Guid& id, StringView name = {}) {
        return CreateEntityInternal(id, name);
    }

    // Destroys an entity (immediate). No-op if the handle is stale.
    void DestroyEntity(EntityHandle entity) {
        if (!IsValid(entity)) { return; }
        DestroyEntityImmediate(entity);
    }

    // True iff the handle refers to a live entity AND its generation matches (i.e. the
    // slot has not been destroyed/reused since the handle was issued).
    [[nodiscard]] bool IsValid(EntityHandle entity) const noexcept {
        if (!entity.IsAssigned() || entity.index >= m_entities.Size()) { return false; }
        const EntitySlot& slot = m_entities[entity.index];
        return slot.alive && slot.generation == entity.generation;
    }

    [[nodiscard]] Guid GetEntityId(EntityHandle entity) const {
        return IsValid(entity) ? m_entities[entity.index].persistentId : Guid{};
    }

    // Finds a live entity by persistent Guid, or Invalid(). Prunes a stale map entry.
    [[nodiscard]] EntityHandle FindEntity(const Guid& id) {
        EntityHandle* found = m_idMap.Find(id);
        if (found == nullptr) { return EntityHandle::Invalid(); }
        if (IsValid(*found)) { return *found; }
        m_idMap.Remove(id);
        return EntityHandle::Invalid();
    }

    [[nodiscard]] StringView GetEntityName(EntityHandle entity) const {
        return IsValid(entity) ? m_entities[entity.index].name.AsView() : StringView{};
    }
    void SetEntityName(EntityHandle entity, StringView name) {
        if (!IsValid(entity)) { return; }
        m_entities[entity.index].name = String(name);
    }

    [[nodiscard]] bool IsActive(EntityHandle entity) const noexcept {
        return IsValid(entity) && m_entities[entity.index].active;
    }
    void SetActive(EntityHandle entity, bool active) {
        if (!IsValid(entity)) { return; }
        m_entities[entity.index].active = active;
        // (Phase 3) notify per-scene systems so component active-state stays in sync.
    }

    // Visits every live entity. `fn` is invoked with the entity's handle.
    template <typename Fn>
    void ForEachEntity(Fn&& fn) const {
        for (u32 i = 0; i < m_entities.Size(); ++i) {
            const EntitySlot& slot = m_entities[i];
            if (slot.alive) { fn(EntityHandle{ i, slot.generation }); }
        }
    }

    // Bumped on every structural change (create/destroy). Lets long-lived iterators or
    // caches detect that the entity table moved underneath them.
    [[nodiscard]] u64 Revision() const noexcept { return m_revision; }

protected:
    // One entity pool slot. `name` is empty when the entity is unnamed.
    struct EntitySlot {
        u32    generation = 0;
        bool   active     = false;
        bool   alive      = false;
        Guid   persistentId;
        String name;
    };

    EntityHandle CreateEntityInternal(const Guid& id, StringView name) {
        u32 index;
        if (!m_freeList.IsEmpty()) {
            index = m_freeList.Back();
            m_freeList.PopBack();
        } else {
            index = static_cast<u32>(m_entities.Size());
            m_entities.PushBack(EntitySlot{});
        }

        EntitySlot& slot = m_entities[index];
        ++slot.generation;
        slot.alive  = true;
        slot.active = true;
        slot.persistentId = id;
        slot.name = name.IsEmpty() ? String{} : String(name);

        ++m_aliveCount;
        ++m_revision;

        const EntityHandle handle{ index, slot.generation };
        m_idMap.InsertOrAssign(id, handle);
        // (Phase 2) initialize transform + append to the root list.
        return handle;
    }

    void DestroyEntityImmediate(EntityHandle entity) {
        EntitySlot& slot = m_entities[entity.index];
        // (Phase 2/3) destroy children recursively + notify systems before freeing.
        m_idMap.Remove(slot.persistentId);
        slot.alive  = false;
        slot.active = false;
        slot.name   = String{};
        slot.persistentId = Guid{};
        m_freeList.PushBack(entity.index);
        --m_aliveCount;
        ++m_revision;
    }

    IAllocator* m_allocator;
    String      m_name;
    Array<EntitySlot> m_entities;            // entity pool (index = slot)
    Array<u32>        m_freeList;            // free slot indices for reuse
    HashMap<Guid, EntityHandle> m_idMap;     // persistent Guid -> live handle
    Random            m_rng;                 // entity Guid source (deterministic seed)
    u32               m_aliveCount = 0;
    u64               m_revision   = 0;
};

} // namespace raptor::scene
