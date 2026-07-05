/// Draconic::Scene - the `:scene` partition.
///
/// Scene: an isolated world of entities with a transform hierarchy (and, in later
/// phases, per-scene systems). Two parallel pools indexed by entity slot:
///   * the entity table - generation-guarded, free-list reuse, persistent-Guid <->
///     handle map, active/name state.
///   * the transform hierarchy - local TRS + cached world matrix per entity, in a
///     doubly-linked parent/child/sibling tree with O(1) splice (head+tail+back
///     pointers), a dirty-flag cascade, and a two-pass UpdateTransforms that snapshots
///     the previous world matrix (for motion vectors) and recomputes only dirty
///     subtrees. Destruction is recursive (children first) and immediate for now;
///     deferred-during-update destroy lands with the update loop in a later phase.

module;
#include "Core/Prelude.h"
#include <type_traits>

export module draconic.scene:scene;

import draconic.core;
import :entity;
import :phase;
import :system;
import :component;

using namespace draconic::core;

export namespace draconic::scene {

class Scene {
public:
    explicit Scene(StringView name = {}, IAllocator& allocator = DefaultAllocator())
        : m_allocator(&allocator), m_name(name, allocator) {}

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] StringView Name() const noexcept { return m_name.AsView(); }
    void SetName(StringView name) { m_name = String(name); }

    [[nodiscard]] u32 EntityCount() const noexcept { return m_aliveCount; }

    // ---- entity lifecycle ----

    // Creates an entity with a fresh random Guid; starts as a root, active, identity transform.
    EntityHandle CreateEntity(StringView name = {}) {
        return CreateEntityInternal(Guid::Generate(m_rng), name);
    }
    // Creates an entity with a specific Guid (used when loading a scene from disk).
    EntityHandle CreateEntity(const Guid& id, StringView name = {}) {
        return CreateEntityInternal(id, name);
    }

    // Destroys an entity and its whole subtree. No-op if the handle is stale. If called
    // during Update (a system destroying entities), destruction is deferred to the
    // frame's Cleanup so iteration stays stable.
    void DestroyEntity(EntityHandle entity) {
        if (!IsValid(entity)) { return; }
        if (m_isUpdating) { m_pendingDestroys.PushBack(entity); return; }
        DestroyEntityImmediate(entity);
    }

    [[nodiscard]] bool IsValid(EntityHandle entity) const noexcept {
        if (!entity.IsAssigned() || entity.index >= m_entities.Size()) { return false; }
        const EntitySlot& slot = m_entities[entity.index];
        return slot.alive && slot.generation == entity.generation;
    }

    [[nodiscard]] Guid GetEntityId(EntityHandle entity) const {
        return IsValid(entity) ? m_entities[entity.index].persistentId : Guid{};
    }
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
        for (SceneSystem* s : m_sortedSystems) { s->OnEntityActiveChanged(entity, active); }
    }

    template <typename Fn>
    void ForEachEntity(Fn&& fn) const {
        for (u32 i = 0; i < m_entities.Size(); ++i) {
            if (m_entities[i].alive) { fn(EntityHandle{ i, m_entities[i].generation }); }
        }
    }

    [[nodiscard]] u64 Revision() const noexcept { return m_revision; }

    // ---- transform hierarchy ----

    void SetLocalTransform(EntityHandle entity, const Transform& transform) {
        if (!IsValid(entity)) { return; }
        m_transforms[entity.index].local = transform;
        MarkDirty(entity);
    }
    [[nodiscard]] Transform GetLocalTransform(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].local : Transform{};
    }
    void SetLocalPosition(EntityHandle entity, Vector3 position) {
        if (!IsValid(entity)) { return; }
        m_transforms[entity.index].local.position = position;
        MarkDirty(entity);
    }

    // World matrix from the most recent UpdateTransforms (Identity until first update).
    [[nodiscard]] Matrix4 GetWorldMatrix(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].worldMatrix : Matrix4::Identity();
    }
    [[nodiscard]] Matrix4 GetPrevWorldMatrix(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].prevWorldMatrix : Matrix4::Identity();
    }
    // Translation row of the world matrix (row-vector convention).
    [[nodiscard]] Vector3 GetWorldPosition(EntityHandle entity) const {
        const Matrix4 w = GetWorldMatrix(entity);
        return Vector3{ w.m[3][0], w.m[3][1], w.m[3][2] };
    }
    // True iff the world matrix was recomputed in the most recent UpdateTransforms
    // (moved, reparented, or a dirty ancestor cascaded through it). Read in PostTransform.
    [[nodiscard]] bool IsTransformUpdatedThisFrame(EntityHandle entity) const {
        return IsValid(entity) && m_transforms[entity.index].updatedThisFrame;
    }

    [[nodiscard]] EntityHandle GetParent(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].parent : EntityHandle::Invalid();
    }
    [[nodiscard]] EntityHandle GetFirstChild(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].firstChild : EntityHandle::Invalid();
    }
    [[nodiscard]] EntityHandle GetNextSibling(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].nextSibling : EntityHandle::Invalid();
    }
    [[nodiscard]] EntityHandle FirstRoot() const noexcept { return m_firstRoot; }

    [[nodiscard]] u32 GetChildCount(EntityHandle entity) const {
        if (!IsValid(entity)) { return 0; }
        u32 count = 0;
        EntityHandle child = m_transforms[entity.index].firstChild;
        while (child.IsAssigned() && IsValid(child)) { ++count; child = m_transforms[child.index].nextSibling; }
        return count;
    }

    // Reparents `child` under `parent` (Invalid() = make a root). Rejects a reparent
    // that would form a cycle (parent is `child` or a descendant of it).
    void SetParent(EntityHandle child, EntityHandle parent) {
        if (!IsValid(child)) { return; }
        if (parent.IsAssigned() && !IsValid(parent)) { return; }
        if (child == parent) { return; }
        if (parent.IsAssigned() && IsDescendantOf(parent, child)) { return; }   // cycle guard

        RemoveFromParent(child);
        m_transforms[child.index].parent = parent;
        if (parent.IsAssigned()) {
            TransformData& p = m_transforms[parent.index];
            AppendToList(child, p.firstChild, p.lastChild);
        } else {
            AppendToList(child, m_firstRoot, m_lastRoot);
        }
        MarkDirty(child);
    }

    // Two-pass world-matrix update: clear last frame's "updated" flags + snapshot the
    // previous world matrix for entities that stopped moving, then recompute only the
    // dirty roots and their subtrees (depth-first, parent before child).
    void UpdateTransforms() {
        const u32 count = static_cast<u32>(m_transforms.Size());
        if (count == 0) { return; }

        for (u32 idx : m_transformsUpdatedThisFrame) {
            if (idx >= m_transforms.Size()) { continue; }
            TransformData& d = m_transforms[idx];
            d.updatedThisFrame = false;
            if (!d.dirty && m_entities[idx].alive) { d.prevWorldMatrix = d.worldMatrix; }
        }
        m_transformsUpdatedThisFrame.Clear();

        for (u32 i = 0; i < count; ++i) {
            if (m_transforms[i].dirty && m_entities[i].alive && !m_transforms[i].parent.IsAssigned()) {
                UpdateTransformRecursive(i, Matrix4::Identity());
            }
        }
    }

    // Entity indices whose world matrix was recomputed in the most recent UpdateTransforms.
    // Lifetime hazard: an index here may belong to an entity destroyed afterwards - gate
    // reads on IsValid(handle). Rewritten each UpdateTransforms.
    [[nodiscard]] Span<const u32> TransformsUpdatedThisFrame() const noexcept {
        return { m_transformsUpdatedThisFrame.Data(), m_transformsUpdatedThisFrame.Size() };
    }

    // ---- per-scene systems ----

    // Constructs + adds a system of type T (one per type); the Scene owns it. Returns a
    // borrowed pointer. Runs OnSceneCreate immediately.
    template <typename T, typename... Args>
    T* AddSystem(Args&&... args) {
        static_assert(std::is_base_of_v<SceneSystem, T>, "T must derive from SceneSystem");
        T* system = m_allocator->New<T>(Forward<Args>(args)...);
        m_systems.PushBack(UniquePtr<SceneSystem>(static_cast<SceneSystem*>(system), *m_allocator));
        m_systemsByType.InsertOrAssign(&TypeOf<T>(), static_cast<SceneSystem*>(system));
        InsertSortedSystem(static_cast<SceneSystem*>(system));
        system->OnSceneCreate(*this);
        return system;
    }

    template <typename T>
    [[nodiscard]] T* GetSystem() noexcept {
        SceneSystem* const* found = m_systemsByType.Find(&TypeOf<T>());
        return (found != nullptr) ? static_cast<T*>(*found) : nullptr;
    }
    template <typename T>
    [[nodiscard]] bool HasSystem() const noexcept { return m_systemsByType.Contains(&TypeOf<T>()); }

    // Visits every component manager (systems that are managers), in UpdateOrder.
    template <typename Fn>
    void ForEachManager(Fn&& fn) {
        for (SceneSystem* s : m_sortedSystems) {
            if (ComponentManagerBase* m = s->AsComponentManager()) { fn(*m); }
        }
    }
    // The serializable manager with this on-disk type id, or null (used on scene load
    // to route a component record to its pool).
    [[nodiscard]] ComponentManagerBase* FindManagerBySerializationId(StringView typeId) {
        for (SceneSystem* s : m_sortedSystems) {
            if (ComponentManagerBase* m = s->AsComponentManager()) {
                if (m->IsSerializable() && m->SerializationTypeId() == typeId) { return m; }
            }
        }
        return nullptr;
    }

    // ---- play / edit state ----

    [[nodiscard]] bool IsStarted() const noexcept { return m_started; }
    [[nodiscard]] bool SimulationEnabled() const noexcept { return m_simulationEnabled; }
    void SetSimulationEnabled(bool enabled) noexcept { m_simulationEnabled = enabled; }

    // Enters play mode: simulation on, notify systems. (Editor "stop" calls Stop.)
    void Start() {
        if (m_started) { return; }
        m_started = true;
        m_simulationEnabled = true;
        for (SceneSystem* s : m_sortedSystems) { s->OnSceneStarted(); }
    }
    void Stop() {
        if (!m_started) { return; }
        for (SceneSystem* s : m_sortedSystems) { s->OnSceneStopped(); }
        m_started = false;
    }

    // ---- update loop ----

    // Runs one frame: initialize pending components, the gameplay phases, the transform
    // recompute, render/spatial extraction, then deferred destruction. Phases run in
    // ScenePhase order; within a phase, systems run in UpdateOrder. Simulation-only
    // systems are skipped while SimulationEnabled is false.
    void Update(f32 deltaTime) {
        m_isUpdating = true;
        InitializePendingComponents();                       // ScenePhase::Initialize
        RunPhase(ScenePhase::PreUpdate, deltaTime);
        RunPhase(ScenePhase::Update, deltaTime);
        RunPhase(ScenePhase::AsyncUpdate, deltaTime);
        RunPhase(ScenePhase::PostUpdate, deltaTime);
        UpdateTransforms();                                  // ScenePhase::TransformUpdate (internal)
        RunPhase(ScenePhase::PostTransform, deltaTime);
        m_isUpdating = false;
        ProcessPendingDestroys();                            // ScenePhase::Cleanup
    }

    void FixedUpdate(f32 fixedDeltaTime) {
        for (SceneSystem* s : m_sortedSystems) {
            if (s->IsSimulationOnly() && !m_simulationEnabled) { continue; }
            s->OnFixedUpdate(fixedDeltaTime);
        }
    }

    // Initializes components added since the last call (deferred init), across all managers.
    void InitializePendingComponents() {
        for (SceneSystem* s : m_sortedSystems) {
            if (ComponentManagerBase* mgr = s->AsComponentManager()) { mgr->InitializePendingComponents(); }
        }
    }

protected:
    struct EntitySlot {
        u32    generation = 0;
        bool   active     = false;
        bool   alive      = false;
        Guid   persistentId;
        String name;
    };

    // Per-entity transform node: local TRS + cached world/prev-world matrices, and the
    // doubly-linked parent/child/sibling pointers (head+tail+back for O(1) splice).
    struct TransformData {
        Transform    local;
        Matrix4         worldMatrix     = Matrix4::Identity();
        Matrix4         prevWorldMatrix = Matrix4::Identity();
        EntityHandle parent      = EntityHandle::Invalid();
        EntityHandle firstChild  = EntityHandle::Invalid();
        EntityHandle lastChild   = EntityHandle::Invalid();
        EntityHandle nextSibling = EntityHandle::Invalid();
        EntityHandle prevSibling = EntityHandle::Invalid();
        bool         dirty            = false;
        bool         updatedThisFrame = false;
    };

    EntityHandle CreateEntityInternal(const Guid& id, StringView name) {
        u32 index;
        if (!m_freeList.IsEmpty()) {
            index = m_freeList.Back();
            m_freeList.PopBack();
        } else {
            index = static_cast<u32>(m_entities.Size());
            m_entities.PushBack(EntitySlot{});
            m_transforms.PushBack(TransformData{});
        }

        EntitySlot& slot = m_entities[index];
        ++slot.generation;
        slot.alive  = true;
        slot.active = true;
        slot.persistentId = id;
        slot.name = name.IsEmpty() ? String{} : String(name);

        m_transforms[index] = TransformData{};   // identity local, no links, not dirty

        ++m_aliveCount;
        ++m_revision;

        const EntityHandle handle{ index, slot.generation };
        m_idMap.InsertOrAssign(id, handle);
        AppendToList(handle, m_firstRoot, m_lastRoot);   // new entities start as roots
        return handle;
    }

    void DestroyEntityImmediate(EntityHandle entity) {
        const u32 index = entity.index;

        // Destroy the subtree first (snapshot the next sibling before each child dies).
        EntityHandle child = m_transforms[index].firstChild;
        while (child.IsAssigned()) {
            const EntityHandle nextSibling = IsValid(child) ? m_transforms[child.index].nextSibling
                                                            : EntityHandle::Invalid();
            DestroyEntityImmediate(child);
            child = nextSibling;
        }

        RemoveFromParent(entity);
        for (SceneSystem* s : m_sortedSystems) { s->OnEntityDestroyed(entity); }   // managers free components

        EntitySlot& slot = m_entities[index];
        m_idMap.Remove(slot.persistentId);
        slot.alive  = false;
        slot.active = false;
        slot.name   = String{};
        slot.persistentId = Guid{};
        m_freeList.PushBack(index);
        --m_aliveCount;
        ++m_revision;

        m_transforms[index] = TransformData{};
    }

    // Marks an entity dirty, cascading to its whole subtree AND up to its ancestors
    // (UpdateTransforms only walks dirty *roots*, so a dirty child needs a dirty root).
    void MarkDirty(EntityHandle entity) {
        if (!entity.IsAssigned()) { return; }
        TransformData& d = m_transforms[entity.index];
        if (d.dirty) { return; }
        d.dirty = true;
        EntityHandle child = d.firstChild;
        while (child.IsAssigned() && IsValid(child)) {
            const EntityHandle next = m_transforms[child.index].nextSibling;
            MarkDirty(child);
            child = next;
        }
        if (d.parent.IsAssigned()) { MarkDirty(d.parent); }
    }

    void UpdateTransformRecursive(u32 index, const Matrix4& parentWorld) {
        {
            TransformData& d = m_transforms[index];
            d.prevWorldMatrix    = d.worldMatrix;
            d.worldMatrix        = d.local.ToMatrix() * parentWorld;
            d.dirty              = false;
            d.updatedThisFrame   = true;
        }
        m_transformsUpdatedThisFrame.PushBack(index);

        const Matrix4 myWorld = m_transforms[index].worldMatrix;
        EntityHandle child = m_transforms[index].firstChild;
        while (child.IsAssigned() && IsValid(child)) {
            const u32 ci = child.index;
            UpdateTransformRecursive(ci, myWorld);
            child = m_transforms[ci].nextSibling;
        }
    }

    // O(1) append to a (head, tail) sibling list - tail pointer avoids an O(n) walk.
    void AppendToList(EntityHandle entity, EntityHandle& head, EntityHandle& tail) {
        TransformData& e = m_transforms[entity.index];
        e.nextSibling = EntityHandle::Invalid();
        e.prevSibling = tail;
        if (!head.IsAssigned()) { head = entity; tail = entity; return; }
        m_transforms[tail.index].nextSibling = entity;
        tail = entity;
    }

    // O(1) splice-out using the back pointer (no walk to find the predecessor).
    void RemoveFromParent(EntityHandle child) {
        TransformData& c = m_transforms[child.index];
        const EntityHandle parent = c.parent;
        const EntityHandle prev = c.prevSibling;
        const EntityHandle next = c.nextSibling;
        if (prev.IsAssigned()) { m_transforms[prev.index].nextSibling = next; }
        if (next.IsAssigned()) { m_transforms[next.index].prevSibling = prev; }
        if (parent.IsAssigned()) {
            TransformData& p = m_transforms[parent.index];
            if (p.firstChild == child) { p.firstChild = next; }
            if (p.lastChild == child)  { p.lastChild = prev; }
        } else {
            if (m_firstRoot == child) { m_firstRoot = next; }
            if (m_lastRoot == child)  { m_lastRoot = prev; }
        }
        c.parent      = EntityHandle::Invalid();
        c.nextSibling = EntityHandle::Invalid();
        c.prevSibling = EntityHandle::Invalid();
    }

    // Whether `entity` is `ancestor` or somewhere below it (walks up from entity).
    [[nodiscard]] bool IsDescendantOf(EntityHandle entity, EntityHandle ancestor) const {
        EntityHandle cur = entity;
        while (cur.IsAssigned() && IsValid(cur)) {
            if (cur == ancestor) { return true; }
            cur = m_transforms[cur.index].parent;
        }
        return false;
    }

    // Runs one gameplay phase across systems in UpdateOrder, honoring sim gating.
    void RunPhase(ScenePhase phase, f32 deltaTime) {
        for (SceneSystem* s : m_sortedSystems) {
            if (s->IsSimulationOnly() && !m_simulationEnabled) { continue; }
            s->OnUpdate(phase, deltaTime);
        }
    }

    // Insertion sort into m_sortedSystems, ascending by UpdateOrder (stable).
    void InsertSortedSystem(SceneSystem* system) {
        m_sortedSystems.PushBack(system);
        usize i = m_sortedSystems.Size() - 1;
        while (i > 0 && m_sortedSystems[i - 1]->UpdateOrder() > system->UpdateOrder()) {
            m_sortedSystems[i] = m_sortedSystems[i - 1];
            m_sortedSystems[i - 1] = system;
            --i;
        }
    }

    // Destroys entities queued during Update (snapshot, since destroying a subtree can
    // be re-entrant). Stale/duplicate entries are skipped by the IsValid guard.
    void ProcessPendingDestroys() {
        if (m_pendingDestroys.IsEmpty()) { return; }
        Array<EntityHandle> batch = Move(m_pendingDestroys);
        m_pendingDestroys = Array<EntityHandle>{};
        for (EntityHandle e : batch) {
            if (IsValid(e)) { DestroyEntityImmediate(e); }
        }
    }

    IAllocator* m_allocator;
    String      m_name;
    Array<EntitySlot>    m_entities;        // entity pool (index = slot)
    Array<TransformData> m_transforms;      // parallel transform pool (same index)
    Array<u32>           m_freeList;        // free slot indices for reuse
    HashMap<Guid, EntityHandle> m_idMap;    // persistent Guid -> live handle
    Array<u32>           m_transformsUpdatedThisFrame;
    EntityHandle         m_firstRoot = EntityHandle::Invalid();
    EntityHandle         m_lastRoot  = EntityHandle::Invalid();
    Random               m_rng;
    u32                  m_aliveCount = 0;
    u64                  m_revision   = 0;

    // per-scene systems
    Array<UniquePtr<SceneSystem>>            m_systems;        // ownership
    HashMap<const TypeInfo*, SceneSystem*>   m_systemsByType;  // lookup by type
    Array<SceneSystem*>                      m_sortedSystems;  // non-owning, UpdateOrder-sorted
    Array<EntityHandle>                      m_pendingDestroys;
    bool                 m_isUpdating       = false;
    bool                 m_started          = false;
    bool                 m_simulationEnabled = true;
};

} // namespace draconic::scene
