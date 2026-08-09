// Foundation::Json - `foundation.json` (hand-rolled JSON DOM: value + parser + writer, UTF-8).
//
// The engine's boundary codec for JSON - the MCP wire protocol and game-chosen data interchange.
// Deliberately NOT an engine data format (no ISerializer backend; engine data stays XML). The value
// type is RTTI-reflected (see :reflection) so scripts can parse/build/query/stringify JSON through
// the standard bound-object machinery. Value semantics throughout - no interior pointers escape.

export module foundation.json;

export import :value;
export import :writer;
export import :parser;
export import :reflection;

// The two convenience members declared on JsonValue live here (the primary interface), where both
// :writer and :parser are visible.
namespace foundation::json
{
    inline foundation::core::String JsonValue::ToString(bool pretty) const
    {
        return Write(*this, pretty);
    }

    inline JsonValue JsonValue::Parse(foundation::core::StringView text)
    {
        return foundation::json::Parse(text).value; // Null on error - script never sees a half-value
    }
}
