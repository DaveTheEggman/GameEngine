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
        // The RNG is deterministic and loading a scene does NOT advance it past the loaded
        // entities, so a fresh id can reproduce a loaded one - re-roll until free (the same
        // collision the content DB re-rolls; three entities sharing a guid corrupts saves).
        Guid id = Guid::Generate(m_rng);
        while (FindEntity(id).IsAssigned()) { id = Guid::Generate(m_rng); }
        return CreateEntityInternal(id, name);
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
        ++m_revision;
    }

    [[nodiscard]] bool IsActive(EntityHandle entity) const noexcept {
        return IsValid(entity) && m_entities[entity.index].active;
    }
    void SetActive(EntityHandle entity, bool active) {
        if (!IsValid(entity)) { return; }
        if (m_entities[entity.index].active == active) { return; }
        m_entities[entity.index].active = active;
        ++m_revision;
        for (SceneSystem* s : m_sortedSystems) { s->OnEntityActiveChanged(entity, active); }
    }

    template <typename Fn>
    void ForEachEntity(Fn&& fn) const {
        for (u32 i = 0; i < m_entities.Size(); ++i) {
            if (m_entities[i].alive) { fn(EntityHandle{ i, m_entities[i].generation }); }
        }
    }

    [[nodiscard]] u64 Revision() const noexcept { return m_revision; }

    // ---- prefab instances (runtime-only bookkeeping; scene.resource drives it) ----
    //
    // One state record per spawned prefab instance. `sourceIds[i]` is the member's guid in
    // the PREFAB PAYLOAD (the stable delta key), `liveIds[i]` its guid in THIS scene.
    // Baselines capture the template state as of spawn (per-member local transform +
    // per-component serialized blob); the scene serializer diffs live state against them at
    // save time, so overrides are DERIVED - nothing tracks edits, and undo/redo can never
    // desynchronize the override set. Never serialized as-is: the scene serializer persists
    // instances as ref+deltas (or expanded, for snapshots) and rebuilds this state on load.
    struct PrefabComponentBaseline {
        Guid   sourceEntity;
        String typeId;        // the owning manager's SerializationTypeId
        Array<u8> blob;       // binary WriteComponent capture at spawn
    };
    struct PrefabInstanceState {
        Guid prefabId;                       // the prefab asset/product instance guid
        Guid rootEntityId;                   // live guid of the instance's root entity
        Array<Guid>      sourceIds;          // payload guids (parallel to liveIds)
        Array<Guid>      liveIds;
        Array<Transform> baselineTransforms; // parallel to sourceIds (template local transforms)
        Array<PrefabComponentBaseline> componentBaselines;

        // NESTING (P4): an instance spawned BY another instance's payload record links to its
        // owner; nestedRootSourceId is this instance's stable identity in the owner's
        // namespace (the payload record's root id) - rebuilds match on it. Nil = top-level.
        Guid ownerRootEntityId{};
        Guid nestedRootSourceId{};
        // Top-level only, TRANSIENT (recomputed at spawn): every prefab id this instance's
        // payload consumed (own + nested records) - template edits to any of them rebuild
        // this instance through its owner payload.
        Array<Guid> referencedPrefabIds;
    };

    void AddPrefabInstance(UniquePtr<PrefabInstanceState> state)
    {
        if (state) { m_prefabInstances.PushBack(static_cast<UniquePtr<PrefabInstanceState>&&>(state)); }
    }

    [[nodiscard]] PrefabInstanceState* FindPrefabInstanceByRoot(const Guid& rootEntityId)
    {
        for (auto& s : m_prefabInstances) { if (s->rootEntityId == rootEntityId) { return s.Get(); } }
        return nullptr;
    }

    /// Removes the state whose root is `rootEntityId` (the entities are the caller's business).
    void RemovePrefabInstance(const Guid& rootEntityId)
    {
        for (usize i = 0; i < m_prefabInstances.Size(); ++i)
        {
            if (m_prefabInstances[i]->rootEntityId == rootEntityId) { m_prefabInstances.RemoveAt(i); return; }
        }
    }

    /// Visits every instance whose ROOT still resolves; states whose root entity was
    /// destroyed are pruned lazily here (no destroy-hook bookkeeping).
    template <typename Fn>
    void ForEachPrefabInstance(Fn&& fn)
    {
        usize w = 0;
        for (usize i = 0; i < m_prefabInstances.Size(); ++i)
        {
            if (!FindEntity(m_prefabInstances[i]->rootEntityId).IsAssigned()) { continue; }   // prune
            if (w != i) { m_prefabInstances[w] = static_cast<UniquePtr<PrefabInstanceState>&&>(m_prefabInstances[i]); }
            fn(*m_prefabInstances[w]);
            ++w;
        }
        m_prefabInstances.Resize(w);
    }

    [[nodiscard]] usize PrefabInstanceCount() const noexcept { return m_prefabInstances.Size(); }

    /// Drops ALL prefab-instance bookkeeping (snapshot restore repopulates from the stream).
    void ClearPrefabInstances() { m_prefabInstances.Clear(); }

    // A prefab instance READ from a scene stream, awaiting its payload: the scene serializer
    // can't resolve prefab assets itself (no DB access), so it parks descriptors here and
    // ResolveScenePrefabs (scene.resource) respawns them with a caller-supplied resolver -
    // the same two-phase shape as component resource Refs + ResolveSceneResources.
    struct PendingPrefabComponentOp {
        Guid   sourceEntity;
        String typeId;
        u8     op = 0;        // 0 = modify, 1 = add, 2 = remove (blob empty)
        Array<u8> blob;       // binary component payload for modify/add
    };
    struct PendingPrefabInstance {
        Guid prefabId;
        Guid parentEntityId;                 // nil = scene root
        Transform rootTransform;
        Array<Guid> sourceIds;               // parallel member guid map
        Array<Guid> liveIds;
        Array<Guid> destroyedMembers;        // source ids the user deleted from the instance
        Array<Guid>      overrideTransformIds;   // source ids with transform overrides
        Array<Transform> overrideTransforms;
        Array<PendingPrefabComponentOp> componentOps;
        // NESTING (P4): mirror of PrefabInstanceState's links (see there). rootLiveId is the
        // instance root's live id (in a PAYLOAD record: the owner-namespace id nested scene
        // records match against).
        Guid rootLiveId{};
        Guid ownerRootEntityId{};
        Guid nestedRootSourceId{};
        // The sibling immediately AFTER the root at capture time (nil = it was the last
        // child): spawn appends records after the plain members, then restores list order
        // from this link. Namespace follows the record's (owner template / live scene).
        Guid nextSiblingId{};
        // False = the root transform matched its baseline at capture: the scene never moved
        // this NESTED instance, so on respawn the owner TEMPLATE's placement wins (a moved
        // root is a scene override and re-applies). Top-level placements always apply.
        // Revert forces it false. Serialized with the record since the order/placement wire
        // change (no pre-change compat).
        bool applyPlacement = true;
    };

    void AddPendingPrefabInstance(UniquePtr<PendingPrefabInstance> pending)
    {
        if (pending) { m_pendingPrefabs.PushBack(static_cast<UniquePtr<PendingPrefabInstance>&&>(pending)); }
    }
    [[nodiscard]] Array<UniquePtr<PendingPrefabInstance>> TakePendingPrefabInstances()
    {
        Array<UniquePtr<PendingPrefabInstance>> out = static_cast<Array<UniquePtr<PendingPrefabInstance>>&&>(m_pendingPrefabs);
        m_pendingPrefabs = Array<UniquePtr<PendingPrefabInstance>>{};
        return out;
    }
    [[nodiscard]] usize PendingPrefabInstanceCount() const noexcept { return m_pendingPrefabs.Size(); }
    /// Non-consuming walk of the parked pendings (transcode re-emits them verbatim so a
    /// load->save cycle without ResolveScenePrefabs stays lossless).
    void ForEachPendingPrefabInstance(const Function<void(PendingPrefabInstance&)>& fn)
    {
        for (const UniquePtr<PendingPrefabInstance>& p : m_pendingPrefabs) { if (p) { fn(*p); } }
    }

    // ---- transform hierarchy ----

    void SetLocalTransform(EntityHandle entity, const Transform& transform) {
        if (!IsValid(entity)) { return; }
        m_transforms[entity.index].local = transform;
        MarkDirty(entity);
    }
    [[nodiscard]] Transform GetLocalTransform(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].local : Transform{};
    }
    void SetLocalPosition(EntityHandle entity, Float3 position) {
        if (!IsValid(entity)) { return; }
        m_transforms[entity.index].local.position = position;
        MarkDirty(entity);
    }

    // World matrix from the most recent UpdateTransforms (Identity until first update).
    [[nodiscard]] Float4x4 GetWorldMatrix(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].worldMatrix : Float4x4::Identity();
    }
    [[nodiscard]] Float4x4 GetPrevWorldMatrix(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].prevWorldMatrix : Float4x4::Identity();
    }
    // Translation row of the world matrix (row-vector convention).
    [[nodiscard]] Float3 GetWorldPosition(EntityHandle entity) const {
        const Float4x4 w = GetWorldMatrix(entity);
        return Float3{ w.m[3][0], w.m[3][1], w.m[3][2] };
    }
    // True iff the world matrix was recomputed in the most recent UpdateTransforms
    // (moved, reparented, or a dirty ancestor cascaded through it). Read in PostTransform.
    [[nodiscard]] bool IsTransformUpdatedThisFrame(EntityHandle entity) const {
        return IsValid(entity) && m_transforms[entity.index].updatedThisFrame;
    }

    [[nodiscard]] EntityHandle GetParent(EntityHandle entity) const {
        return IsValid(entity) ? m_transforms[entity.index].parent : EntityHandle::Invalid();
    }
    /// First entity in the ROOT sibling list (walk with GetNextSibling for list order - the
    /// order the hierarchy displays and serialization preserves).
    [[nodiscard]] EntityHandle GetFirstRoot() const noexcept { return m_firstRoot; }
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
        ++m_revision;
    }

    // World-preserving reparent (editor semantics: the entity stays put in the world; its LOCAL
    // transform is recomputed relative to the new parent via TRS decompose). World matrices are
    // composed fresh from the local chain, so this is correct even with dirty cached transforms.
    // No-op when the underlying move is refused (cycle etc.).
    void SetParent(EntityHandle child, EntityHandle parent, bool keepWorldTransform) {
        if (!keepWorldTransform) { SetParent(child, parent); return; }
        if (!IsValid(child)) { return; }
        const Float4x4 childWorld = ComposeWorldMatrix(child);
        const u64 before = m_revision;
        SetParent(child, parent);
        if (m_revision != before) { ApplyWorldAsLocal(child, childWorld); }
    }

    void MoveBefore(EntityHandle child, EntityHandle sibling, bool keepWorldTransform) {
        if (!keepWorldTransform) { MoveBefore(child, sibling); return; }
        if (!IsValid(child)) { return; }
        const Float4x4 childWorld = ComposeWorldMatrix(child);
        const u64 before = m_revision;
        MoveBefore(child, sibling);
        if (m_revision != before) { ApplyWorldAsLocal(child, childWorld); }
    }

    // Fresh world matrix composed up the parent chain (independent of the cached worldMatrix,
    // which is only current after UpdateTransforms).
    [[nodiscard]] Float4x4 ComposeWorldMatrix(EntityHandle entity) const {
        Float4x4 world = Float4x4::Identity();
        for (EntityHandle e = entity; IsValid(e); e = m_transforms[e.index].parent) {
            world = world * m_transforms[e.index].local.ToMatrix();
        }
        return world;
    }

    // Sibling ORDERING: moves `child` under `sibling`'s parent, immediately BEFORE `sibling`
    // (into the root list when `sibling` is a root). Same guards as SetParent: both must be
    // valid, and moving an entity below its own subtree (cycle) is refused. O(1).
    // (SetParent(child, parent) is the companion "append at END of parent" operation - it
    // re-appends even when the parent is unchanged, i.e. it doubles as move-to-end.)
    void MoveBefore(EntityHandle child, EntityHandle sibling) {
        if (!IsValid(child) || !IsValid(sibling)) { return; }
        if (child == sibling) { return; }
        if (m_transforms[sibling.index].prevSibling == child) { return; }   // already there
        const EntityHandle parent = m_transforms[sibling.index].parent;
        if (parent.IsAssigned() && IsDescendantOf(parent, child)) { return; }   // cycle guard

        RemoveFromParent(child);
        TransformData& c = m_transforms[child.index];
        TransformData& s = m_transforms[sibling.index];
        c.parent = parent;
        c.nextSibling = sibling;
        c.prevSibling = s.prevSibling;
        if (s.prevSibling.IsAssigned()) {
            m_transforms[s.prevSibling.index].nextSibling = child;
        } else if (parent.IsAssigned()) {
            m_transforms[parent.index].firstChild = child;
        } else {
            m_firstRoot = child;
        }
        s.prevSibling = child;
        MarkDirty(child);
        ++m_revision;
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

        // Recurse from every dirty-subtree TOP: a dirty entity whose parent is clean (or who
        // has none). MarkDirty propagates down, so interior dirty nodes always have a dirty
        // parent - but a freshly REPARENTED entity under a clean parent is a top that the old
        // roots-only scan missed (its world matrix stayed stale until something moved the
        // parent; pasted/duplicated children rendered at the origin).
        for (u32 i = 0; i < count; ++i) {
            const TransformData& d = m_transforms[i];
            if (!d.dirty || !m_entities[i].alive) { continue; }
            if (!d.parent.IsAssigned()) {
                UpdateTransformRecursive(i, Float4x4::Identity());
            }
            else if (!m_transforms[d.parent.index].dirty) {
                // The parent is clean, so its cached world matrix is current.
                UpdateTransformRecursive(i, m_transforms[d.parent.index].worldMatrix);
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
    // Visits every system (managers AND plain systems), in UpdateOrder - the editor's
    // scene-settings inspector and SerializeScene walk systems with settings blocks.
    template <typename Fn>
    void ForEachSystem(Fn&& fn) {
        for (SceneSystem* s : m_sortedSystems) { fn(*s); }
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

    // ---- per-scene time (the scene IS the simulatable unit) ----
    // Each scene owns its time scale and fixed-step accumulator, so pausing/slowing one
    // scene (the Game tab) never affects another (a Simulate page beside it). The
    // effective frame dt a scene sees = host dt x context TimeScale x scene TimeScale
    // (the SceneSubsystem applies the scene factor; the context factor is already in
    // the dt it receives).

    void SetTimeScale(f32 scale) noexcept { m_timeScale = scale < 0.0f ? 0.0f : scale; }
    [[nodiscard]] f32 TimeScale() const noexcept { return m_timeScale; }

    /// Fixed-lane configuration (step seconds + spiral-of-death clamp).
    void SetFixedTiming(f32 step, u32 maxSteps) noexcept
    {
        m_stepper.step = step;
        m_stepper.maxSteps = maxSteps;
    }
    [[nodiscard]] f32 FixedTimeStep() const noexcept { return m_stepper.step; }
    /// Interpolation weight for fixed-rate consumers (physics pose smoothing);
    /// published by AdvanceTime after its steps.
    [[nodiscard]] f32 FixedAlpha() const noexcept { return m_fixedAlpha; }

    /// The frame drive (SceneSubsystem::BeginFrame): accumulates `scaledDelta` (already
    /// context- AND scene-scaled), runs FixedUpdate for each whole step, publishes the
    /// leftover as FixedAlpha. Tests wanting exact-step control call FixedUpdate directly.
    u32 AdvanceTime(f32 scaledDelta)
    {
        const u32 steps = m_stepper.Advance(scaledDelta);
        for (u32 i = 0; i < steps; ++i) { FixedUpdate(m_stepper.step); }
        m_fixedAlpha = m_stepper.Alpha();
        return steps;
    }

    // Enters play mode: simulation on, notify systems. (Editor "stop" calls Stop.)
    void Start() {
        if (m_started) { return; }
        // Authored locals -> world matrices BEFORE systems hear OnSceneStarted: world
        // matrices are Identity until the first update, and start callbacks that sample
        // them (physics body building, spawn points) must see the authored layout.
        UpdateTransforms();
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
        Float4x4         worldMatrix     = Float4x4::Identity();
        Float4x4         prevWorldMatrix = Float4x4::Identity();
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

    // Rewrite `child`'s LOCAL transform so its world matrix equals `childWorld` under its
    // CURRENT parent (the keep-world half of a reparent). Row-vector: local = world * parent⁻¹.
    void ApplyWorldAsLocal(EntityHandle child, const Float4x4& childWorld) {
        const EntityHandle parent = m_transforms[child.index].parent;
        const Float4x4 localMat = parent.IsAssigned()
            ? childWorld * Inverse(ComposeWorldMatrix(parent))
            : childWorld;
        SetLocalTransform(child, Transform::FromMatrix(localMat));
    }

    void UpdateTransformRecursive(u32 index, const Float4x4& parentWorld) {
        {
            TransformData& d = m_transforms[index];
            d.prevWorldMatrix    = d.worldMatrix;
            d.worldMatrix        = d.local.ToMatrix() * parentWorld;
            d.dirty              = false;
            d.updatedThisFrame   = true;
        }
        m_transformsUpdatedThisFrame.PushBack(index);

        const Float4x4 myWorld = m_transforms[index].worldMatrix;
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
    Array<UniquePtr<PrefabInstanceState>> m_prefabInstances;
    Array<UniquePtr<PendingPrefabInstance>> m_pendingPrefabs;
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
    f32                  m_timeScale = 1.0f;
    core::FixedStepper   m_stepper;
    f32                  m_fixedAlpha = 0.0f;
};

} // namespace draconic::scene
