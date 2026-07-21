// Draconic::NetReplication - implementation unit: the field codec + the cached replicated-property
// layout harvest. Free functions (no reflect bodies), but kept out of the interface unit so the
// static layout cache + the type-dispatch table live in one TU.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

module draconic.net.replication;

import draconic.core;
import draconic.net;
import draconic.scene;

using namespace draconic::core;

namespace draconic::net {

namespace {
    // Reflected property/type names + attribute keys are ASCII const char*; borrow them as UTF-8
    // views (StringView is char8_t-based; the reflection layer stores/compares keys the same way).
    [[nodiscard]] StringView Ascii(const char* s) noexcept {
        return s != nullptr ? StringView(reinterpret_cast<const char8_t*>(s)) : StringView{};
    }
    [[nodiscard]] const Attribute* FindReplicatedMark(const PropertyInfo& property) noexcept {
        return FindAttribute(property, Ascii(kReplicatedAttribute));
    }

    // Length-prefixed UTF-8 on the wire (the component type tag - SerializationTypeId).
    void WriteWireString(BitWriter& writer, StringView s) {
        writer.WriteVarU32(static_cast<u32>(s.Size()));
        writer.WriteBytes(Span<const byte>(reinterpret_cast<const byte*>(s.Data()), s.Size()));
    }
    [[nodiscard]] String ReadWireString(BitReader& reader) {
        const u32 n = reader.ReadVarU32();
        if (n == 0 || !reader.Ok()) { return String{}; }
        Array<byte> buf; buf.Resize(n);
        reader.ReadBytes(Span<byte>(buf.Data(), buf.Size()));
        return String(StringView(reinterpret_cast<const char8_t*>(buf.Data()), n));
    }

    // FNV-1a over the component's on-disk type id - the baseline key (same family as RpcTable::Hash).
    [[nodiscard]] u32 HashTypeId(StringView s) noexcept {
        u32 h = 2166136261u;
        for (usize i = 0; i < s.Size(); ++i) { h ^= static_cast<u8>(s.Data()[i]); h *= 16777619u; }
        return h;
    }
    [[nodiscard]] bool BlobsEqual(const Array<byte>& a, Span<const byte> b) noexcept {
        if (a.Size() != b.Size()) { return false; }
        for (usize i = 0; i < a.Size(); ++i) { if (a[i] != b[i]) { return false; } }
        return true;
    }
    [[nodiscard]] Array<byte> CopyBlob(Span<const byte> b) {
        Array<byte> out; out.Resize(b.Size());
        for (usize i = 0; i < b.Size(); ++i) { out[i] = b[i]; }
        return out;
    }
}

bool IsFieldTypeSupported(const TypeInfo* type) noexcept {
    if (type == nullptr) { return false; }
    return type == &TypeOf<bool>()
        || type == &TypeOf<f32>()  || type == &TypeOf<f64>()
        || type == &TypeOf<i8>()   || type == &TypeOf<u8>()
        || type == &TypeOf<i16>()  || type == &TypeOf<u16>()
        || type == &TypeOf<i32>()  || type == &TypeOf<u32>()
        || type == &TypeOf<i64>()  || type == &TypeOf<u64>()
        || type == &TypeOf<Float2>() || type == &TypeOf<Float3>()
        || type == &TypeOf<Float4>() || type == &TypeOf<Quaternion>();
}

bool IsReplicated(const PropertyInfo& property) {
    return FindReplicatedMark(property) != nullptr && IsFieldTypeSupported(property.type);
}

Span<const PropertyInfo* const> ReplicatedProperties(const TypeInfo& type) {
    // Single-threaded (fixed lane). Cache the harvested layout per type; the const PropertyInfo*
    // are stable (they live in the type's static TypeData).
    static HashMap<const TypeInfo*, Array<const PropertyInfo*>> cache;
    if (const Array<const PropertyInfo*>* found = cache.Find(&type)) {
        return Span<const PropertyInfo* const>{ found->Data(), found->Size() };
    }

    Array<const PropertyInfo*> layout;
    for (const PropertyInfo& property : Properties(type)) {
        if (FindReplicatedMark(property) == nullptr) { continue; }
        if (!IsFieldTypeSupported(property.type)) {
            DRACONIC_LOG_WARNING(u8"Net",
                u8"replicated field '{}' on '{}' has an unsupported type - excluded from replication",
                Ascii(property.name), Ascii(type.name));
            continue;
        }
        layout.PushBack(&property);
    }
    cache.InsertOrAssign(&type, Move(layout));
    const Array<const PropertyInfo*>& stored = *cache.Find(&type);
    return Span<const PropertyInfo* const>{ stored.Data(), stored.Size() };
}

bool WriteFieldValue(BitWriter& writer, const Variant& value) {
    if (const bool* v = value.TryGet<bool>()) { writer.WriteBool(*v); return true; }
    if (const f32* v = value.TryGet<f32>())   { writer.WriteFloat(*v); return true; }
    if (const f64* v = value.TryGet<f64>())   { u64 bits = 0; MemCopy(&bits, v, sizeof(bits)); writer.WriteU64(bits); return true; }
    if (const i8* v = value.TryGet<i8>())     { writer.WriteU8(static_cast<u8>(*v)); return true; }
    if (const u8* v = value.TryGet<u8>())     { writer.WriteU8(*v); return true; }
    if (const i16* v = value.TryGet<i16>())   { writer.WriteU16(static_cast<u16>(*v)); return true; }
    if (const u16* v = value.TryGet<u16>())   { writer.WriteU16(*v); return true; }
    if (const i32* v = value.TryGet<i32>())   { writer.WriteI32(*v); return true; }
    if (const u32* v = value.TryGet<u32>())   { writer.WriteU32(*v); return true; }
    if (const i64* v = value.TryGet<i64>())   { writer.WriteU64(static_cast<u64>(*v)); return true; }
    if (const u64* v = value.TryGet<u64>())   { writer.WriteU64(*v); return true; }
    if (const Float2* v = value.TryGet<Float2>()) { writer.WriteFloat(v->x); writer.WriteFloat(v->y); return true; }
    if (const Float3* v = value.TryGet<Float3>()) { writer.WriteFloat(v->x); writer.WriteFloat(v->y); writer.WriteFloat(v->z); return true; }
    if (const Float4* v = value.TryGet<Float4>()) { writer.WriteFloat(v->x); writer.WriteFloat(v->y); writer.WriteFloat(v->z); writer.WriteFloat(v->w); return true; }
    if (const Quaternion* v = value.TryGet<Quaternion>()) { writer.WriteFloat(v->x); writer.WriteFloat(v->y); writer.WriteFloat(v->z); writer.WriteFloat(v->w); return true; }
    return false;
}

bool ReadFieldValue(BitReader& reader, const TypeInfo* type, Variant& out) {
    if (type == &TypeOf<bool>()) { out = Variant::From<bool>(reader.ReadBool()); return true; }
    if (type == &TypeOf<f32>())  { out = Variant::From<f32>(reader.ReadFloat()); return true; }
    if (type == &TypeOf<f64>())  { const u64 bits = reader.ReadU64(); f64 v = 0.0; MemCopy(&v, &bits, sizeof(v)); out = Variant::From<f64>(v); return true; }
    if (type == &TypeOf<i8>())   { out = Variant::From<i8>(static_cast<i8>(reader.ReadU8())); return true; }
    if (type == &TypeOf<u8>())   { out = Variant::From<u8>(reader.ReadU8()); return true; }
    if (type == &TypeOf<i16>())  { out = Variant::From<i16>(static_cast<i16>(reader.ReadU16())); return true; }
    if (type == &TypeOf<u16>())  { out = Variant::From<u16>(reader.ReadU16()); return true; }
    if (type == &TypeOf<i32>())  { out = Variant::From<i32>(reader.ReadI32()); return true; }
    if (type == &TypeOf<u32>())  { out = Variant::From<u32>(reader.ReadU32()); return true; }
    if (type == &TypeOf<i64>())  { out = Variant::From<i64>(static_cast<i64>(reader.ReadU64())); return true; }
    if (type == &TypeOf<u64>())  { out = Variant::From<u64>(reader.ReadU64()); return true; }
    if (type == &TypeOf<Float2>()) { Float2 v; v.x = reader.ReadFloat(); v.y = reader.ReadFloat(); out = Variant::From<Float2>(v); return true; }
    if (type == &TypeOf<Float3>()) { Float3 v; v.x = reader.ReadFloat(); v.y = reader.ReadFloat(); v.z = reader.ReadFloat(); out = Variant::From<Float3>(v); return true; }
    if (type == &TypeOf<Float4>()) { Float4 v; v.x = reader.ReadFloat(); v.y = reader.ReadFloat(); v.z = reader.ReadFloat(); v.w = reader.ReadFloat(); out = Variant::From<Float4>(v); return true; }
    if (type == &TypeOf<Quaternion>()) { Quaternion v; v.x = reader.ReadFloat(); v.y = reader.ReadFloat(); v.z = reader.ReadFloat(); v.w = reader.ReadFloat(); out = Variant::From<Quaternion>(v); return true; }
    return false;
}

usize WriteReplicatedState(BitWriter& writer, const Instance& instance) {
    if (instance.Type() == nullptr) { return 0; }
    usize written = 0;
    for (const PropertyInfo* property : ReplicatedProperties(*instance.Type())) {
        const Variant value = GetProperty(*property, instance);
        if (WriteFieldValue(writer, value)) { ++written; }
    }
    return written;
}

usize ReadReplicatedState(BitReader& reader, const Instance& instance) {
    if (instance.Type() == nullptr) { return 0; }
    usize applied = 0;
    for (const PropertyInfo* property : ReplicatedProperties(*instance.Type())) {
        Variant value;
        if (!ReadFieldValue(reader, property->type, value)) { break; }
        if (!reader.Ok()) { break; }   // ran past the end - don't apply a garbage read; caller sees the short count
        (void)SetProperty(*property, instance, value);
        ++applied;
    }
    return applied;
}

// ---- NetworkComponent reflection (versioned records need the patched TypeInfo) ---------------

DRACONIC_REFLECT_VALUE(NetworkComponent, "draconic::net")
{
    builder.DataVersion(1);
    builder.Property<&NetworkComponent::id>("id");
    builder.Property<&NetworkComponent::authority>("authority");
}

void RegisterReplicationComponents()
{
    static const bool once = []() {
        DraconicRegisterValue_NetworkComponent();
        GlobalTypeRegistry().Register(TypeOf<NetworkComponent>());
        return true;
    }();
    (void)once;
}

// ---- StateReplication -----------------------------------------------------------------------

NetworkId StateReplication::AssignNetworkId(dscene::Scene& scene, dscene::EntityHandle entity)
{
    auto* netMgr = scene.GetSystem<NetworkComponentManager>();
    if (netMgr == nullptr) { return NetworkId::Invalid(); }
    NetworkComponent& nc = netMgr->Has(entity) ? *netMgr->Get(entity) : netMgr->Add(entity);
    if (!nc.id.IsValid()) { nc.id = NetworkId{ ++m_nextNetworkId }; }   // 0 stays "unassigned"
    m_netIdToEntity.InsertOrAssign(nc.id.value, entity);
    return nc.id;
}

dscene::EntityHandle StateReplication::FindEntity(NetworkId id) const
{
    if (const dscene::EntityHandle* found = m_netIdToEntity.Find(id.value)) { return *found; }
    return dscene::EntityHandle::Invalid();
}

dscene::EntityHandle StateReplication::FindOrCreateEntity(dscene::Scene& scene, u32 networkId)
{
    if (const dscene::EntityHandle* found = m_netIdToEntity.Find(networkId)) {
        if (scene.IsValid(*found)) { return *found; }
    }
    const dscene::EntityHandle e = scene.CreateEntity();
    if (auto* netMgr = scene.GetSystem<NetworkComponentManager>()) {
        NetworkComponent& nc = netMgr->Has(e) ? *netMgr->Get(e) : netMgr->Add(e);
        nc.id = NetworkId{ networkId };
        nc.authority = NetworkAuthority::Server;   // the client's view: the server owns this entity
    }
    m_netIdToEntity.InsertOrAssign(networkId, e);
    return e;
}

void StateReplication::CaptureSnapshot(dscene::Scene& scene, BitWriter& out)
{
    auto* netMgr = scene.GetSystem<NetworkComponentManager>();
    if (netMgr == nullptr) { out.WriteVarU32(0); return; }

    // Snapshot the assigned networked entities from the tag pool.
    struct Ent { u32 id; dscene::EntityHandle handle; };
    Array<Ent> entities;
    netMgr->ForEach([&](NetworkComponent& nc, dscene::EntityHandle e) {
        if (nc.id.IsValid()) { entities.PushBack(Ent{ nc.id.value, e }); }
    });

    out.WriteVarU32(static_cast<u32>(entities.Size()));
    for (const Ent& ent : entities) {
        out.WriteU32(ent.id);
        // Gather this entity's serializable components that carry replicated fields.
        Array<dscene::ComponentManagerBase*> comps;
        scene.ForEachManager([&](dscene::ComponentManagerBase& m) {
            const Instance inst = m.GetComponentInstance(ent.handle);
            if (inst.Type() == nullptr) { return; }
            if (ReplicatedProperties(*inst.Type()).IsEmpty()) { return; }
            if (!m.IsSerializable() || m.SerializationTypeId().IsEmpty()) { return; }  // need a wire tag
            comps.PushBack(&m);
        });
        out.WriteVarU32(static_cast<u32>(comps.Size()));
        for (dscene::ComponentManagerBase* m : comps) {
            // Length-prefix each component blob so a peer lacking the type can skip it (forward-compat).
            BitWriter fields;
            (void)WriteReplicatedState(fields, m->GetComponentInstance(ent.handle));
            const Span<const byte> blob = fields.Data();
            WriteWireString(out, m->SerializationTypeId());
            out.WriteVarU32(static_cast<u32>(blob.Size()));
            out.WriteBytes(blob);
        }
    }
}

void StateReplication::ApplyComponentRecords(dscene::Scene& scene, dscene::EntityHandle entity,
                                             u32 count, BitReader& in)
{
    for (u32 j = 0; j < count && in.Ok(); ++j) {
        const String typeId = ReadWireString(in);
        const u32 blobBytes = in.ReadVarU32();
        Array<byte> blob;
        blob.Resize(blobBytes);
        if (blobBytes > 0) { in.ReadBytes(Span<byte>(blob.Data(), blob.Size())); }
        if (!in.Ok()) { break; }

        dscene::ComponentManagerBase* m = scene.FindManagerBySerializationId(typeId.AsView());
        if (m == nullptr) { continue; }   // unknown type on this peer - blob already consumed (skip)
        if (m->GetComponentInstance(entity).Type() == nullptr) { (void)m->AddDefaultComponent(entity); }
        const Instance inst = m->GetComponentInstance(entity);
        if (inst.Type() == nullptr) { continue; }
        BitReader fields(Span<const byte>(blob.Data(), blob.Size()));
        (void)ReadReplicatedState(fields, inst);
    }
}

void StateReplication::ApplySnapshot(dscene::Scene& scene, BitReader& in)
{
    const u32 entityCount = in.ReadVarU32();
    for (u32 i = 0; i < entityCount && in.Ok(); ++i) {
        const u32 networkId = in.ReadU32();
        const dscene::EntityHandle entity = FindOrCreateEntity(scene, networkId);
        const u32 componentCount = in.ReadVarU32();
        ApplyComponentRecords(scene, entity, componentCount, in);
    }
}

void StateReplication::ForgetPeer(u32 peerId) { (void)m_peerBaselines.Remove(peerId); }

usize StateReplication::CaptureDelta(dscene::Scene& scene, u32 peerId, BitWriter& out)
{
    auto* netMgr = scene.GetSystem<NetworkComponentManager>();
    if (netMgr == nullptr) { out.WriteVarU32(0); return 0; }

    // Get-or-insert this peer's baseline.
    if (m_peerBaselines.Find(peerId) == nullptr) { m_peerBaselines.InsertOrAssign(peerId, PeerBaseline{}); }
    PeerBaseline& base = *m_peerBaselines.Find(peerId);

    // One delta entry: a changed/new entity (its changed component records) or a removal.
    struct CompRecord { StringView typeId; Array<byte> blob; };
    struct Entry { u32 id = 0; bool removed = false; Array<CompRecord> comps; };
    Array<Entry> entries;
    HashMap<u32, EntityBaseline> nextBaseline;   // becomes the baseline after this capture

    netMgr->ForEach([&](NetworkComponent& nc, dscene::EntityHandle e) {
        if (!nc.id.IsValid()) { return; }
        const u32 nid = nc.id.value;
        const EntityBaseline* oldEb = base.entities.Find(nid);

        EntityBaseline newEb;
        Array<CompRecord> changed;
        scene.ForEachManager([&](dscene::ComponentManagerBase& m) {
            const Instance inst = m.GetComponentInstance(e);
            if (inst.Type() == nullptr) { return; }
            if (ReplicatedProperties(*inst.Type()).IsEmpty()) { return; }
            if (!m.IsSerializable() || m.SerializationTypeId().IsEmpty()) { return; }

            BitWriter fields;
            (void)WriteReplicatedState(fields, inst);
            const Span<const byte> blob = fields.Data();
            const u32 typeHash = HashTypeId(m.SerializationTypeId());

            // Changed if the component is new to this peer or its bytes differ from last-sent.
            const Array<byte>* prev = nullptr;
            if (oldEb != nullptr) {
                for (const ComponentBaseline& cb : oldEb->components) {
                    if (cb.typeHash == typeHash) { prev = &cb.blob; break; }
                }
            }
            if (prev == nullptr || !BlobsEqual(*prev, blob)) {
                changed.PushBack(CompRecord{ m.SerializationTypeId(), CopyBlob(blob) });
            }
            newEb.components.PushBack(ComponentBaseline{ typeHash, CopyBlob(blob) });
        });

        if (oldEb == nullptr || !changed.IsEmpty()) {   // new entity, or something changed
            entries.PushBack(Entry{ nid, false, Move(changed) });
        }
        nextBaseline.InsertOrAssign(nid, Move(newEb));
    });

    // Removals: entities in the old baseline that are no longer networked/present.
    for (const auto& entry : base.entities) {
        if (nextBaseline.Find(entry.key) == nullptr) {
            entries.PushBack(Entry{ entry.key, true, {} });
        }
    }

    out.WriteVarU32(static_cast<u32>(entries.Size()));
    for (const Entry& entry : entries) {
        out.WriteU32(entry.id);
        out.WriteU8(entry.removed ? 1u : 0u);
        if (entry.removed) { continue; }
        out.WriteVarU32(static_cast<u32>(entry.comps.Size()));
        for (const CompRecord& comp : entry.comps) {
            WriteWireString(out, comp.typeId);
            out.WriteVarU32(static_cast<u32>(comp.blob.Size()));
            out.WriteBytes(Span<const byte>(comp.blob.Data(), comp.blob.Size()));
        }
    }

    base.entities = Move(nextBaseline);   // commit last-sent (reliable-ordered => delivered)
    return entries.Size();
}

void StateReplication::ApplyDelta(dscene::Scene& scene, BitReader& in)
{
    const u32 entryCount = in.ReadVarU32();
    for (u32 i = 0; i < entryCount && in.Ok(); ++i) {
        const u32 networkId = in.ReadU32();
        const u8 flags = in.ReadU8();
        if (!in.Ok()) { break; }
        if ((flags & 1u) != 0u) {   // removed
            if (const dscene::EntityHandle* h = m_netIdToEntity.Find(networkId)) {
                if (scene.IsValid(*h)) { scene.DestroyEntity(*h); }
            }
            (void)m_netIdToEntity.Remove(networkId);
            continue;
        }
        const dscene::EntityHandle entity = FindOrCreateEntity(scene, networkId);
        const u32 componentCount = in.ReadVarU32();
        ApplyComponentRecords(scene, entity, componentCount, in);
    }
}

}
