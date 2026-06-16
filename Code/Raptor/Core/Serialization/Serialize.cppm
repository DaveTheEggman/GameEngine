// Raptor Core — :serialize partition
//
// Serialize(ISerializer&, T&) free functions for Core's own types: one path
// runs either direction. (RTTI-driven auto-walking lives in an external
// Serialization module, per §4.7.)

module;
#include "Core/Prelude.h"
#include <type_traits>

export module raptor.core:serialize;

import :base;
import :serializer;
import :math;
import :matrix;
import :string;
import :array;

export namespace raptor::core
{
    // -----------------------------------------------------------------------
    // Serialize() — describe a type's data once; runs either direction.
    // -----------------------------------------------------------------------

    // Arithmetic and enum types: raw bytes.
    template <typename T>
        requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    void Serialize(ISerializer& ar, T& value)
    {
        (void)ar.SerializeRaw(&value, sizeof(T));
    }

    // Math value types (trivially copyable).
    inline void Serialize(ISerializer& ar, Vec2& v) { (void)ar.SerializeRaw(&v, sizeof(v)); }
    inline void Serialize(ISerializer& ar, Vec3& v) { (void)ar.SerializeRaw(&v, sizeof(v)); }
    inline void Serialize(ISerializer& ar, Vec4& v) { (void)ar.SerializeRaw(&v, sizeof(v)); }
    inline void Serialize(ISerializer& ar, Quat& q) { (void)ar.SerializeRaw(&q, sizeof(q)); }
    inline void Serialize(ISerializer& ar, Mat4& m) { (void)ar.SerializeRaw(&m, sizeof(m)); }

    // String: length-prefixed character data.
    template <typename CharT>
    void Serialize(ISerializer& ar, BasicString<CharT>& str)
    {
        u32 length = static_cast<u32>(str.Size());
        Serialize(ar, length);

        if (ar.IsLoading())
        {
            str.Clear();
            str.Reserve(length);
            for (u32 i = 0; i < length; ++i)
            {
                CharT ch{};
                (void)ar.SerializeRaw(&ch, sizeof(CharT));
                str.PushBack(ch);
            }
        }
        else
        {
            for (u32 i = 0; i < length; ++i)
            {
                CharT ch = str[i];
                (void)ar.SerializeRaw(&ch, sizeof(CharT));
            }
        }
    }

    // Array: count-prefixed, each element serialized via Serialize().
    template <typename T>
    void Serialize(ISerializer& ar, Array<T>& array)
    {
        u32 count = static_cast<u32>(array.Size());
        Serialize(ar, count);

        if (ar.IsLoading())
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
    }
}
