// Raptor Core — :serialization partition
//
// The serialization contract (§4.7): one direction-aware path serves both load
// and save. ISerializer is the pure interface; Serializer is the concrete base
// holding direction/version/error; BinarySerializer is a backend over IStream.
// Serialize(ISerializer&, T&) free functions describe a type's data once.
//
// NOTE: the RTTI-driven auto-walker (serialize via reflected properties) and
// richer formats live in an external Serialization module, not Core (§4.7).
// Core provides the contract + Serialize for its own types.

module;
#include "Core/Prelude.h"
#include <type_traits>

export module raptor.core:serialization;

import :base;
import :array;
import :string;
import :io;
import :math;
import :matrix;

export namespace raptor::core
{
    enum class SerializeDirection
    {
        Load,
        Save,
    };

    // Pure interface: direction + the single raw-bytes primitive.
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        [[nodiscard]] virtual SerializeDirection Direction() const noexcept = 0;
        [[nodiscard]] virtual u32 Version() const noexcept = 0;

        // Moves `size` bytes between memory and the backing store, in whichever
        // direction this serializer runs.
        virtual Status SerializeRaw(void* data, usize size) = 0;

        [[nodiscard]] bool IsLoading() const noexcept { return Direction() == SerializeDirection::Load; }
        [[nodiscard]] bool IsSaving() const noexcept { return Direction() == SerializeDirection::Save; }
    };

    // Concrete base: holds direction, version, and a sticky error status.
    // Backends extend this, not ISerializer directly.
    class Serializer : public ISerializer
    {
    public:
        explicit Serializer(SerializeDirection direction) noexcept : m_direction(direction) {}

        [[nodiscard]] SerializeDirection Direction() const noexcept override { return m_direction; }
        [[nodiscard]] u32 Version() const noexcept override { return m_version; }
        void SetVersion(u32 version) noexcept { m_version = version; }

        [[nodiscard]] Status GetStatus() const noexcept { return m_status; }
        [[nodiscard]] bool IsOk() const noexcept { return m_status.IsOk(); }

    protected:
        void Fail(ErrorCode code) noexcept
        {
            if (m_status.IsOk()) { m_status = code; }
        }

        SerializeDirection m_direction;
        u32 m_version = 0;
        Status m_status{};
    };

    // Backend: raw little-endian-as-stored binary over an IStream.
    class BinarySerializer final : public Serializer
    {
    public:
        BinarySerializer(IStream& stream, SerializeDirection direction) noexcept
            : Serializer(direction), m_stream(&stream) {}

        Status SerializeRaw(void* data, usize size) override
        {
            if (size == 0) { return Status{}; }

            if (IsSaving())
            {
                if (m_stream->Write(data, size) != size) { Fail(ErrorCode::Internal); }
            }
            else
            {
                if (m_stream->Read(data, size) != size) { Fail(ErrorCode::Internal); }
            }
            return m_status;
        }

    private:
        IStream* m_stream;
    };

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
