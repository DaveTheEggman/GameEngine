// Raptor Core — :serialize partition
//
// Serialize(ISerializer&, T&) free functions for Core's own types: describe a
// type's data once, runs either direction and works for any backend (binary or
// keyed/text). Aggregates use named fields / array scopes so text output stays
// readable and binary output stays endianness-portable. (RTTI-driven
// auto-walking lives in an external Serialization module, per §4.7.)

module;
#include "Core/Prelude.h"
#include <type_traits>

export module raptor.core:serialize;

import :base;
import :serializer;
import :iserializable;
import :math;
import :vec2;
import :vec3;
import :vec4;
import :mat4;
import :quat;
import :string;
import :array;

export namespace raptor::core
{
    // -----------------------------------------------------------------------
    // Scalar type mapping — C++ type to ScalarKind (enums via underlying type).
    // -----------------------------------------------------------------------
    template <typename T>
    constexpr ScalarKind ScalarKindOf() noexcept
    {
        if constexpr (std::is_enum_v<T>)
        {
            // Resolve via the underlying type (only instantiated for enums).
            return ScalarKindOf<std::underlying_type_t<T>>();
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return ScalarKind::Bool;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            return sizeof(T) == 4 ? ScalarKind::Float32 : ScalarKind::Float64;
        }
        else if constexpr (std::is_signed_v<T>)
        {
            if constexpr (sizeof(T) == 1) { return ScalarKind::Int8; }
            else if constexpr (sizeof(T) == 2) { return ScalarKind::Int16; }
            else if constexpr (sizeof(T) == 4) { return ScalarKind::Int32; }
            else { return ScalarKind::Int64; }
        }
        else
        {
            if constexpr (sizeof(T) == 1) { return ScalarKind::UInt8; }
            else if constexpr (sizeof(T) == 2) { return ScalarKind::UInt16; }
            else if constexpr (sizeof(T) == 4) { return ScalarKind::UInt32; }
            else { return ScalarKind::UInt64; }
        }
    }

    // -----------------------------------------------------------------------
    // Serialize() — describe a type's data once; runs either direction.
    // -----------------------------------------------------------------------

    // Arithmetic and enum types: one typed scalar.
    template <typename T>
        requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    void Serialize(ISerializer& ar, T& value)
    {
        ar.Scalar(&value, ScalarKindOf<T>());
    }

    // Math value types: named fields, so text output is readable and binary is
    // decomposed (no struct-padding / endianness surprises).
    inline void Serialize(ISerializer& ar, Vec2& v)
    {
        ar.BeginObject();
        ar.Key("x"); Serialize(ar, v.x);
        ar.Key("y"); Serialize(ar, v.y);
        ar.EndObject();
    }
    inline void Serialize(ISerializer& ar, Vec3& v)
    {
        ar.BeginObject();
        ar.Key("x"); Serialize(ar, v.x);
        ar.Key("y"); Serialize(ar, v.y);
        ar.Key("z"); Serialize(ar, v.z);
        ar.EndObject();
    }
    inline void Serialize(ISerializer& ar, Vec4& v)
    {
        ar.BeginObject();
        ar.Key("x"); Serialize(ar, v.x);
        ar.Key("y"); Serialize(ar, v.y);
        ar.Key("z"); Serialize(ar, v.z);
        ar.Key("w"); Serialize(ar, v.w);
        ar.EndObject();
    }
    inline void Serialize(ISerializer& ar, Quat& q)
    {
        ar.BeginObject();
        ar.Key("x"); Serialize(ar, q.x);
        ar.Key("y"); Serialize(ar, q.y);
        ar.Key("z"); Serialize(ar, q.z);
        ar.Key("w"); Serialize(ar, q.w);
        ar.EndObject();
    }
    inline void Serialize(ISerializer& ar, Mat4& m)
    {
        // 16 elements, row-major.
        u32 count = 16;
        ar.BeginArray(count);
        const u32 n = count < 16u ? count : 16u;
        for (u32 i = 0; i < n; ++i) { Serialize(ar, m.Data()[i]); }
        ar.EndArray();
    }

    // ISerializable: dispatch to the object's own Serialize(), so serializable
    // members compose with Serialize(ar, "key", member) like any other type.
    inline void Serialize(ISerializer& ar, ISerializable& obj) { obj.Serialize(ar); }

    // String: first-class text.
    inline void Serialize(ISerializer& ar, String& str) { ar.Text(str); }

    // UTF-8 string: transcoded through the wide text channel.
    inline void Serialize(ISerializer& ar, UTF8String& str)
    {
        if (ar.Mode() == SerializeMode::Write)
        {
            String wide = ToWide(str);
            ar.Text(wide);
        }
        else
        {
            String wide;
            ar.Text(wide);
            str = ToUTF8(wide);
        }
    }

    // Array: count-prefixed (array scope), each element serialized via Serialize().
    template <typename T>
    void Serialize(ISerializer& ar, Array<T>& array)
    {
        u32 count = static_cast<u32>(array.Size());
        ar.BeginArray(count);

        if (ar.Mode() == SerializeMode::Read)
        {
            array.Clear();
            array.Reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                T element{};
                Serialize(ar, element);
                array.PushBack(Move(element));
            }
        }
        else
        {
            for (u32 i = 0; i < count; ++i)
            {
                Serialize(ar, array[i]);
            }
        }

        ar.EndArray();
    }

    // Named-field convenience for object members: `Serialize(ar, "key", value)`.
    template <typename T>
    void Serialize(ISerializer& ar, const char* key, T& value)
    {
        ar.Key(key);
        Serialize(ar, value);
    }
}
