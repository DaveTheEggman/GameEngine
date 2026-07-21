/// Draconic::NetReplication - the `draconic.net.replication` module (docs/design/networking.md §5, §7 P2).
///
/// The foundation of StateReplication: a stable per-entity NetworkId + authority, and - the central
/// bet of the design - a REFLECTION-DRIVEN field codec. A component marks properties `Replicated`
/// (via the existing per-property attribute system, so no per-component net code is hand-written),
/// and this layer harvests that layout once per type and streams the marked fields through the
/// `:wire` BitWriter/BitReader. Snapshot assembly, per-peer delta, spawn and relevancy stack on top
/// of this codec in later slices; this unit depends only on Core (reflection) + draconic.net (wire).

module;
#include "Core/Prelude.h"

export module draconic.net.replication;

import draconic.core;
import draconic.net;        // BitWriter / BitReader (:wire)
import draconic.scene;      // Scene / EntityHandle / ComponentManagerBase (snapshot assembly)
import draconic.resource;   // ResourceManager (SerializableComponentManager's ResolveResources seam)

using namespace draconic::core;
namespace dscene = draconic::scene;

export namespace draconic::net {

// ---- identity + authority -------------------------------------------------------------------

// A stable, server-assigned handle for a replicated entity. Wire-carried; 0 = unassigned. Distinct
// from EntityHandle (index+generation, process-local) and from the persisted Guid (disk identity):
// NetworkId is the compact id peers agree on for the lifetime of a networked entity.
struct NetworkId {
    u32 value = 0;
    [[nodiscard]] static constexpr NetworkId Invalid() noexcept { return NetworkId{ 0 }; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const NetworkId& o) const noexcept { return value == o.value; }
    [[nodiscard]] constexpr bool operator!=(const NetworkId& o) const noexcept { return value != o.value; }
};

// Who owns an entity's replicated state. Server-authoritative is the default (§5.6 anti-cheat);
// per-entity Client authority is the host-migration / client-owned-avatar escape hatch.
enum class NetworkAuthority : u8 { Server, Client };

// ---- replicated-field marking (rides the existing reflection attribute system) --------------

// The property-attribute key a component sets to opt a field into replication:
//   builder.Property<&C::position>("position").PropAttribute(kReplicatedAttribute, true);
// ASCII const char* to match TypeBuilder::PropAttribute's key type (attribute keys are stored as
// const char*). The value is reserved for future priority/precision descriptors; presence alone
// means "replicate".
inline constexpr const char* kReplicatedAttribute = "net.replicated";

// True if this property is marked for replication AND its field type is codec-supported. The AND is
// deliberate: an unsupported marked type is EXCLUDED from the layout (with a one-time warning) so a
// server and client harvesting the same TypeInfo always agree on the field set - never a desync.
[[nodiscard]] bool IsReplicated(const PropertyInfo& property);

// True if the field codec can encode a value of this type (bool / the integer widths / f32 / f64 /
// Float2-4 / Quaternion). Enums, strings, Guids and resource Refs are not yet supported.
[[nodiscard]] bool IsFieldTypeSupported(const TypeInfo* type) noexcept;

// The ordered replicated-property layout for a component type, harvested from reflection and cached.
// Both peers derive it from the same TypeInfo, so the order + set match by construction. Empty for a
// type with no replicated fields.
[[nodiscard]] Span<const PropertyInfo* const> ReplicatedProperties(const TypeInfo& type);

// ---- the field codec (Variant <-> wire) -----------------------------------------------------

// Encode one reflected field value. Dispatches on the Variant's held type; returns false (writes
// nothing) for an unsupported type. Kept public for delta/RPC reuse.
[[nodiscard]] bool WriteFieldValue(BitWriter& writer, const Variant& value);

// Decode one reflected field of the given type from the stream into `out`. Returns false (reads
// nothing) for an unsupported type. The type is the property's declared type (the write side is
// self-describing only by position, so the reader must know the layout - it does, from the shared
// harvest).
[[nodiscard]] bool ReadFieldValue(BitReader& reader, const TypeInfo* type, Variant& out);

// ---- component-level state sync --------------------------------------------------------------

// Write every replicated field of `instance` (a live component reached via reflection, e.g. from
// ComponentManagerBase::GetComponentInstance) in layout order. Returns the field count written.
usize WriteReplicatedState(BitWriter& writer, const Instance& instance);

// Read + apply the replicated fields written by WriteReplicatedState back onto `instance`, in the
// same layout order. Returns the field count applied. A read past the end of the stream stops early
// (the wire is overflow-safe); returns what was applied.
usize ReadReplicatedState(BitReader& reader, const Instance& instance);

// ---- the networked-entity tag (a scene component) -------------------------------------------

// Tags an entity as replicated. The server assigns the NetworkId (StateReplication::AssignNetworkId);
// the client mirrors it. Persistent (a designer can mark an entity networked in the editor), so it
// rides SerializableComponentManager - but its OWN fields are identity, not replicated state (they
// carry no kReplicatedAttribute, so the field codec never touches them).
struct NetworkComponent {
    NetworkId id{};
    NetworkAuthority authority = NetworkAuthority::Server;
    Guid prefab{};   // the source prefab for network spawn (nil = a bare / non-prefab networked entity)
};

// ADL serialization for scene persistence (bidirectional; enum via the temp-u8 idiom).
inline void Serialize(ISerializer& ar, NetworkComponent& c) {
    draconic::core::Serialize(ar, "id", c.id.value);
    u8 authority = static_cast<u8>(c.authority);
    draconic::core::Serialize(ar, "authority", authority);
    c.authority = static_cast<NetworkAuthority>(authority);
    draconic::core::Serialize(ar, "prefab", c.prefab);   // the Guid overload routes through ISerializer::GuidValue
}

class NetworkComponentManager final : public dscene::SerializableComponentManager<NetworkComponent> {
public:
    NetworkComponentManager() : SerializableComponentManager(u8"net.Network") {}
};

// Registers NetworkComponent's reflection (call once before a networked scene is built; the
// snapshot path needs the patched TypeInfo for its versioned records). Idempotent.
void RegisterReplicationComponents();

// ---- client-side interpolation (smooth playback of low-rate updates) ------------------------

// True if this field type is smoothly interpolated (vs snapped) by LerpFieldValue.
[[nodiscard]] bool IsInterpolatableType(const TypeInfo* type) noexcept;

// Interpolate one reflected field between two samples, t in [0,1]. f32/f64/Float2-4 lerp;
// Quaternion nlerps (shortest-path); non-interpolatable types (bool/ints) SNAP to `a` - the value
// in effect at the render time (the earlier bracketing sample). Same field type on both sides.
[[nodiscard]] Variant LerpFieldValue(const Variant& a, const Variant& b, f32 t);

// A per-entity, per-component timeline of received replicated states, sampled at a DELAYED render
// time so low-rate updates play back smoothly (Valve/Fiedler snapshot interpolation). Delay-agnostic:
// the caller passes renderTimeMs (= now - interpolationDelay). Record on network receive; Sample each
// frame to write the interpolated fields onto the live component. Genre-neutral.
class InterpolationBuffer {
public:
    void SetHistoryMs(f64 ms) noexcept { m_historyMs = ms; }

    // Snapshot a component's replicated fields at `timestampMs` (layout order, via reflection).
    void Record(NetworkId id, u32 componentTypeHash, f64 timestampMs, const Instance& component);
    // Write the fields interpolated at `renderTimeMs` onto `component`; false if no samples exist.
    // Clamps to the earliest/latest sample outside the buffered window (no extrapolation).
    bool Sample(NetworkId id, u32 componentTypeHash, f64 renderTimeMs, const Instance& component) const;
    // Drop an entity's timelines (despawn / disconnect).
    void Forget(NetworkId id);
    [[nodiscard]] usize TrackedEntities() const noexcept { return m_entities.Size(); }

private:
    struct StateSample { f64 time = 0.0; Array<Variant> fields; };   // fields in replicated-layout order
    struct Timeline { Array<StateSample> samples; };                 // ascending by time
    f64 m_historyMs = 1000.0;
    HashMap<u32, HashMap<u32, Timeline>> m_entities;            // networkId -> (typeHash -> timeline)
};

// ---- the replication model seam + StateReplication ------------------------------------------

// The seam a replication architecture implements (§5.1): StateReplication (server-authoritative
// property snapshots, here) vs the deferred CommandReplication (lockstep). A snapshot is written on
// the server and applied on the client, both over the :wire stream.
class IReplicationModel {
public:
    virtual ~IReplicationModel() = default;
    virtual void CaptureSnapshot(dscene::Scene& scene, BitWriter& out) = 0;
    virtual void ApplySnapshot(dscene::Scene& scene, BitReader& in) = 0;
};

// Server-authoritative state replication. This slice does the FULL snapshot (every networked
// entity's every replicated component) - the primitive that per-peer delta (next slice) and
// full-snapshot late-join build on. Per-peer baselines, interpolation and relevancy are later slices.
//
// A snapshot is: VarU32 entityCount, then per entity { U32 networkId, VarU32 componentCount, then
// per component { string SerializationTypeId, VarU32 blobBytes, blob } }. The per-component
// length prefix lets a peer that lacks a component type SKIP it (forward-compat) instead of
// desyncing the reader.
class StateReplication final : public IReplicationModel {
public:
    // The client's prefab-spawn seam: resolve a network-spawned prefab into a local entity (the host
    // wires this to its content DB via SpawnPrefab). Null (or a nil prefab id) => a bare entity is
    // created instead - enough to round-trip state, but no prefab structure/visuals.
    using SpawnHandler = core::Function<dscene::EntityHandle(dscene::Scene&, const Guid&, NetworkId)>;
    void SetSpawnHandler(SpawnHandler handler);

    // Server: give an entity a NetworkId (adds the NetworkComponent if absent), returning it. A
    // re-registered entity keeps its id. `prefab` records the source prefab so a client can network-
    // spawn it (nil for a bare networked entity). Records the id->entity mapping for capture.
    NetworkId AssignNetworkId(dscene::Scene& scene, dscene::EntityHandle entity, const Guid& prefab = {});

    void CaptureSnapshot(dscene::Scene& scene, BitWriter& out) override;
    void ApplySnapshot(dscene::Scene& scene, BitReader& in) override;

    // Server: write the DELTA for one peer - only the entities/components that changed since this
    // peer's last delta, plus removed entities. Returns the number of entries written (0 = nothing
    // changed). The baseline is "what was last sent this peer", so this rides RELIABLE-ORDERED
    // delivery (§5.6): send the output reliably or the baseline diverges. Updates the peer baseline.
    usize CaptureDelta(dscene::Scene& scene, u32 peerId, BitWriter& out);
    // Client: apply a delta - changed components applied in place, removed entities destroyed.
    void ApplyDelta(dscene::Scene& scene, BitReader& in);
    // Drop a peer's baseline on disconnect; its next CaptureDelta re-sends everything as new.
    void ForgetPeer(u32 peerId);

    // The local entity for a NetworkId (invalid if unknown) - the id->entity map, populated by
    // AssignNetworkId (server) or ApplySnapshot's find-or-create (client).
    [[nodiscard]] dscene::EntityHandle FindEntity(NetworkId id) const;
    [[nodiscard]] usize NetworkedCount() const noexcept { return m_netIdToEntity.Size(); }

private:
    // Client: the entity for this id. First sight of a spawn record with a prefab id routes through
    // the spawn handler (prefab instance); otherwise a bare tagged entity is created.
    dscene::EntityHandle FindOrCreateEntity(dscene::Scene& scene, u32 networkId, const Guid& prefab, bool spawn);
    // Shared apply loop: read `count` component records (tag + length-prefixed blob) onto an entity.
    void ApplyComponentRecords(dscene::Scene& scene, dscene::EntityHandle entity, u32 count, BitReader& in);
    // Read a count-prefixed run of entity records (the unified snapshot/delta payload) and apply them.
    void ApplyEntries(dscene::Scene& scene, BitReader& in);

    // A peer's last-sent state (the delta baseline), per networked entity, per component.
    struct ComponentBaseline { u32 typeHash = 0; Array<byte> blob; };
    struct EntityBaseline { Array<ComponentBaseline> components; };
    struct PeerBaseline { HashMap<u32, EntityBaseline> entities; };   // networkId -> its components

    u32 m_nextNetworkId = 0;   // server-side monotonic id allocator (0 stays "unassigned")
    HashMap<u32, dscene::EntityHandle> m_netIdToEntity;
    HashMap<u32, PeerBaseline> m_peerBaselines;   // peerId -> baseline
    SpawnHandler m_spawnHandler;                  // client-side prefab resolver (null = bare create)
};

}
