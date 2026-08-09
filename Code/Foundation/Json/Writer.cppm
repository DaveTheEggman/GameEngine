// Foundation::Json - :writer partition
//
// Serialize a JsonValue to UTF-8 JSON text: compact (default) or 2-space pretty. Strings are escaped
// per RFC 8259; already-valid UTF-8 bytes pass through unescaped. Non-finite numbers (NaN/Inf, which
// JSON cannot represent) are written as null.

module;
#include "Core/Prelude.h"

export module foundation.json:writer;

import foundation.core;
import :value;

using namespace foundation::core;

namespace foundation::json::detail
{
    inline void WriteEscapedString(StringBuilder& out, StringView s)
    {
        out.Append(u8'"');
        const char8_t* d = s.Data();
        for (usize i = 0; i < s.Size(); ++i)
        {
            const char8_t c = d[i];
            switch (c)
            {
            case u8'"': out.Append(u8"\\\""); break;
            case u8'\\': out.Append(u8"\\\\"); break;
            case u8'\b': out.Append(u8"\\b"); break;
            case u8'\f': out.Append(u8"\\f"); break;
            case u8'\n': out.Append(u8"\\n"); break;
            case u8'\r': out.Append(u8"\\r"); break;
            case u8'\t': out.Append(u8"\\t"); break;
            default:
                if (c < 0x20)
                {
                    // Other control characters -> \u00XX.
                    static constexpr char8_t kHex[] = u8"0123456789abcdef";
                    out.Append(u8"\\u00");
                    out.Append(kHex[(c >> 4) & 0xF]);
                    out.Append(kHex[c & 0xF]);
                }
                else
                {
                    out.Append(static_cast<char8_t>(c));
                }
                break;
            }
        }
        out.Append(u8'"');
    }

    inline void WriteNumber(StringBuilder& out, f64 n)
    {
        // JSON has no NaN/Infinity; degrade to null so the output stays valid.
        if (!(n == n) || n > 1.7976931348623157e308 || n < -1.7976931348623157e308)
        {
            out.Append(u8"null");
            return;
        }
        out.AppendFloat(n);
    }

    inline void WriteIndent(StringBuilder& out, i32 depth)
    {
        out.Append(u8'\n');
        for (i32 i = 0; i < depth; ++i)
        {
            out.Append(u8"  ");
        }
    }

    void WriteValue(StringBuilder& out, const JsonValue& v, bool pretty, i32 depth)
    {
        switch (v.Type())
        {
        case JsonType::Null: out.Append(u8"null"); break;
        case JsonType::Bool: out.Append(v.AsBool() ? u8"true" : u8"false"); break;
        case JsonType::Number: WriteNumber(out, v.AsNumber()); break;
        case JsonType::String:
        {
            const String s = v.AsString();
            WriteEscapedString(out, s.AsView());
            break;
        }
        case JsonType::Array:
        {
            const i64 n = v.Count();
            if (n == 0)
            {
                out.Append(u8"[]");
                break;
            }
            out.Append(u8'[');
            for (i64 i = 0; i < n; ++i)
            {
                if (i != 0)
                {
                    out.Append(u8',');
                }
                if (pretty)
                {
                    WriteIndent(out, depth + 1);
                }
                WriteValue(out, v.At(i), pretty, depth + 1);
            }
            if (pretty)
            {
                WriteIndent(out, depth);
            }
            out.Append(u8']');
            break;
        }
        case JsonType::Object:
        {
            const Array<String>& keys = v.Keys();
            if (keys.Size() == 0)
            {
                out.Append(u8"{}");
                break;
            }
            out.Append(u8'{');
            for (usize i = 0; i < keys.Size(); ++i)
            {
                if (i != 0)
                {
                    out.Append(u8',');
                }
                if (pretty)
                {
                    WriteIndent(out, depth + 1);
                }
                WriteEscapedString(out, keys[i].AsView());
                out.Append(pretty ? u8": " : u8":");
                WriteValue(out, v.Get(keys[i].AsView()), pretty, depth + 1);
            }
            if (pretty)
            {
                WriteIndent(out, depth);
            }
            out.Append(u8'}');
            break;
        }
        }
    }
}

export namespace foundation::json
{
    // Serialize `value` to JSON text. `pretty` = 2-space-indented multi-line; default is compact.
    [[nodiscard]] inline String Write(const JsonValue& value, bool pretty = false)
    {
        StringBuilder out;
        detail::WriteValue(out, value, pretty, 0);
        return out.Take();
    }
}
