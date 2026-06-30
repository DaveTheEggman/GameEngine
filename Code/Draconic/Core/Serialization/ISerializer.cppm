// Draconic Core — :iserializer partition
//
// The serialization contract: a mode-aware, format-agnostic interface. One
// Serialize() path runs either direction, and the interface speaks in *intent*
// (typed scalars, named fields, structured scopes) rather than raw bytes — so a
// binary backend and a keyed/text backend (JSON, …) can both implement it.

module;
#include "Core/Prelude.h"

export module draconic.core:iserializer;

import :base;
import :string;
import :guid;

export namespace draconic::core
{
    enum class SerializeMode
    {
        Read,
        Write,
    };

    // The type of a scalar, so keyed/text backends can emit the right token and
    // binary backends know the width. Decouples "what kind of value" from bytes.
    enum class ScalarKind : u8
    {
        Bool,
        Int8, UInt8,
        Int16, UInt16,
        Int32, UInt32,
        Int64, UInt64,
        Float32, Float64,
    };

    // Format-agnostic pure interface. Backends extend Serializer, not this
    // directly — Serializer supplies no-op defaults for the naming/scope ops
    // that unkeyed formats (e.g. binary) ignore.
    //
    // Keyed/text backends use Key()/object scopes to produce `"name": value`.
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        [[nodiscard]] virtual SerializeMode Mode() const noexcept = 0;
        [[nodiscard]] virtual u32 Version() const noexcept = 0;

        // Names the next value within the current object. Ignored by unkeyed
        // formats; keyed/text formats associate it with the value that follows.
        virtual void Key(const char* name) noexcept = 0;

        // Structured scopes. BeginArray moves the element count (written on
        // write, read on read).
        virtual void BeginObject() = 0;
        virtual void EndObject() = 0;
        virtual void BeginArray(u32& count) = 0;
        virtual void EndArray() = 0;

        // Moves one typed scalar between memory and the backing store.
        virtual void Scalar(void* value, ScalarKind kind) = 0;

        // Moves a string. First-class so text formats store it natively.
        virtual void Text(String& value) = 0;

        // Moves an opaque byte blob (raw in binary; e.g. base64 in text).
        virtual void Blob(void* data, usize size) = 0;

        // Moves a Guid as a backend-chosen primitive (Traktor-style): binary writes the raw 16
        // bytes (compact); text writes the canonical 36-char string (readable, one copyable value).
        // The Serializer base provides the text default; binary overrides for compactness.
        virtual void GuidValue(Guid& value) = 0;
    };
}
