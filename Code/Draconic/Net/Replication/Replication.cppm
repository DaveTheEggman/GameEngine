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
import draconic.net;   // BitWriter / BitReader (:wire)

using namespace draconic::core;

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

}
