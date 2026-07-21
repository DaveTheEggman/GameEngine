// Draconic::NetReplication - implementation unit: the field codec + the cached replicated-property
// layout harvest. Free functions (no reflect bodies), but kept out of the interface unit so the
// static layout cache + the type-dispatch table live in one TU.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module draconic.net.replication;

import draconic.core;
import draconic.net;

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

}
