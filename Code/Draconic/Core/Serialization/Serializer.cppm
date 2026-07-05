// Draconic Core - :serializer partition
//
// Serializer: the concrete base over ISerializer that holds the mode, version,
// and a sticky error Status. Backends (e.g. BinarySerializer) extend this.

module;
#include "Core/Prelude.h"

export module draconic.core:serializer;

export import :iserializer;
import :base;
import :string;
import :guid;

export namespace draconic::core
{
    // Backends extend this, not ISerializer directly.
    class Serializer : public ISerializer
    {
    public:
        explicit Serializer(SerializeMode mode) noexcept : m_mode(mode) {}

        [[nodiscard]] SerializeMode Mode() const noexcept override { return m_mode; }
        [[nodiscard]] u32 Version() const noexcept override { return m_version; }
        void SetVersion(u32 version) noexcept { m_version = version; }

        [[nodiscard]] Status GetStatus() const noexcept { return m_status; }
        [[nodiscard]] bool IsOk() const noexcept { return m_status.IsOk(); }

        [[nodiscard]] bool IsReading() const noexcept { return m_mode == SerializeMode::Read; }
        [[nodiscard]] bool IsWriting() const noexcept { return m_mode == SerializeMode::Write; }

        // Naming and object/array scopes carry no information in unkeyed formats;
        // default them to no-ops. Keyed/text backends override what they need.
        // (BeginArray/Scalar/Text/Blob stay pure - every backend must move data.)
        void Key(const char* name) noexcept override { (void)name; }
        void BeginObject() override {}
        void EndObject() override {}
        void EndArray() override {}

        // Default Guid representation: the canonical 36-char string (readable in text backends).
        // BinarySerializer overrides this with the compact raw-16-bytes form.
        void GuidValue(Guid& value) override
        {
            String text;
            if (m_mode == SerializeMode::Write)
            {
                utf8char buffer[37];
                value.ToChars(buffer);
                text = String{ StringView{ buffer, 36 } };
            }
            Text(text);
            if (m_mode == SerializeMode::Read)
            {
                Guid parsed{};
                if (Guid::TryParse(text.AsView(), parsed)) { value = parsed; }
            }
        }

    protected:
        void Fail(ErrorCode code) noexcept
        {
            if (m_status.IsOk()) { m_status = code; }
        }

        SerializeMode m_mode;
        u32 m_version = 0;
        Status m_status{};
    };
}
