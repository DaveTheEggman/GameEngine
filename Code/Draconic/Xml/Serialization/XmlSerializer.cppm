// Draconic::XmlSerialization - the `draconic.xml.serialization` module.
//
// An XML backend for Core's format-agnostic ISerializer/Serializer contract:
// the first text-based serialization backend. One Serialize() path runs either
// direction; this maps the Key()/scope/Scalar/Text calls onto an XML DOM
// (draconic.xml). Inspired by Sedulous.Serialization.Xml's DOM scheme (typed
// element tags + a `name` attribute), adapted to Draconic's leaner interface.
//
// Layering: kept out of Core (which can't depend on Xml) and out of the XML DOM
// library (which stays serialization-agnostic) - mirrors Sedulous.Serialization.Xml.

module;
#include "Core/Prelude.h"
#include <cstdlib> // strtoll / strtoull / strtod

export module draconic.xml.serialization;

import draconic.core;
import draconic.xml;

using namespace draconic::core;

export namespace draconic::xml
{
    // ISerializer backend over an XML DOM.
    //
    // Layout: each field is a child element. Its tag names the scalar kind
    // ("i32", "f32", "object", "array", …); a keyed field carries a `name`
    // attribute, array elements are positional (unnamed). Scalars/strings store
    // their value as element text; arrays carry a `count` attribute.
    class XmlSerializer final : public Serializer
    {
    public:
        // Write mode: builds a fresh document rooted at <root>.
        XmlSerializer() : Serializer(SerializeMode::Write)
        {
            XmlElement* root = m_doc.CreateElement(u8"root");
            m_doc.AppendChild(root);
            m_writeCurrent = root;
        }

        // Read mode: reads from an already-parsed document (caller owns it).
        explicit XmlSerializer(XmlDocument& source) : Serializer(SerializeMode::Read)
        {
            m_readStack.PushBack(ReadScope{ source.RootElement(), nullptr });
            ResetCursor(m_readStack[0]);
        }

        // Write-mode output (declaration omitted).
        void GetOutput(String& output) const
        {
            XmlWriteSettings settings = XmlWriteSettings::Default();
            settings.OmitDeclaration = true;
            m_doc.WriteTo(output, settings);
        }
        void GetOutput(String& output, XmlWriteSettings settings) const { m_doc.WriteTo(output, settings); }

        // --- ISerializer ---------------------------------------------------
        void Key(const char* name) noexcept override { m_pendingKey = name; }

        void BeginObject() override
        {
            if (IsWriting())
            {
                XmlElement* element = MakeElement(u8"object");
                m_writeStack.PushBack(m_writeCurrent);
                m_writeCurrent = element;
            }
            else
            {
                XmlElement* element = Locate();
                if (element == nullptr) { Fail(ErrorCode::NotFound); return; }
                PushRead(element);
            }
        }

        void EndObject() override
        {
            if (IsWriting())
            {
                if (m_writeStack.Size() == 0) { Fail(ErrorCode::Internal); return; }
                m_writeCurrent = m_writeStack[m_writeStack.Size() - 1];
                m_writeStack.PopBack();
            }
            else
            {
                if (m_readStack.Size() <= 1) { Fail(ErrorCode::Internal); return; }
                m_readStack.PopBack();
            }
        }

        void BeginArray(u32& count) override
        {
            if (IsWriting())
            {
                XmlElement* element = MakeElement(u8"array");
                String countStr; AppendValue(countStr, count);
                element->SetAttribute(u8"count", countStr);
                m_writeStack.PushBack(m_writeCurrent);
                m_writeCurrent = element;
            }
            else
            {
                XmlElement* element = Locate();
                if (element == nullptr) { count = 0; Fail(ErrorCode::NotFound); return; }
                u64 parsed = 0;
                if (!ParseU64(element->GetAttribute(u8"count"), parsed)) { Fail(ErrorCode::Internal); }
                count = static_cast<u32>(parsed);
                PushRead(element);
            }
        }

        void EndArray() override { EndObject(); }

        void Scalar(void* value, ScalarKind kind) override
        {
            if (IsWriting())
            {
                XmlElement* element = MakeElement(ScalarTag(kind));
                String text; WriteScalar(value, kind, text);
                element->SetTextContent(text);
            }
            else
            {
                XmlElement* element = Locate();
                if (element == nullptr) { Fail(ErrorCode::NotFound); return; }
                String text; element->GetTextContent(text);
                if (!ReadScalar(value, kind, text)) { Fail(ErrorCode::Internal); }
            }
        }

        void Text(String& value) override
        {
            if (IsWriting())
            {
                XmlElement* element = MakeElement(u8"string");
                element->SetTextContent(value);
            }
            else
            {
                XmlElement* element = Locate();
                if (element == nullptr) { Fail(ErrorCode::NotFound); return; }
                value.Clear();
                element->GetTextContent(value);
            }
        }

        void Blob(void* data, usize size) override
        {
            if (IsWriting())
            {
                XmlElement* element = MakeElement(u8"blob");
                String hex; EncodeHex(static_cast<const u8*>(data), size, hex);
                element->SetTextContent(hex);
            }
            else
            {
                XmlElement* element = Locate();
                if (element == nullptr) { Fail(ErrorCode::NotFound); return; }
                String hex; element->GetTextContent(hex);
                if (!DecodeHex(hex, static_cast<u8*>(data), size)) { Fail(ErrorCode::Internal); }
            }
        }

    private:
        struct ReadScope { XmlElement* element; XmlNode* cursor; };

        // --- write helpers ---
        XmlElement* MakeElement(StringView tag)
        {
            XmlElement* element = m_doc.CreateElement(tag);
            if (m_pendingKey != nullptr)
            {
                element->SetAttribute(u8"name", StringView(reinterpret_cast<const utf8char*>(m_pendingKey)));
                m_pendingKey = nullptr;
            }
            m_writeCurrent->AppendChild(element);
            return element;
        }

        // --- read helpers ---
        static void ResetCursor(ReadScope& scope)
        {
            scope.cursor = (scope.element != nullptr) ? scope.element->FirstChild() : nullptr;
        }
        void PushRead(XmlElement* element)
        {
            ReadScope scope{ element, nullptr };
            ResetCursor(scope);
            m_readStack.PushBack(scope);
        }

        // Locate the source element for the next read: by `name` if a key is
        // pending, else the next positional (unnamed) child element.
        XmlElement* Locate()
        {
            ReadScope& scope = m_readStack[m_readStack.Size() - 1];
            if (scope.element == nullptr) { m_pendingKey = nullptr; return nullptr; }

            if (m_pendingKey != nullptr)
            {
                const StringView name(reinterpret_cast<const utf8char*>(m_pendingKey));
                m_pendingKey = nullptr;
                for (XmlNode* c = scope.element->FirstChild(); c != nullptr; c = c->NextSibling())
                {
                    if (c->NodeType() == XmlNodeType::Element)
                    {
                        XmlElement* e = static_cast<XmlElement*>(c);
                        if (e->GetAttribute(u8"name") == name) { return e; }
                    }
                }
                return nullptr;
            }

            // Positional: advance the cursor to the next element child.
            while (scope.cursor != nullptr && scope.cursor->NodeType() != XmlNodeType::Element)
            {
                scope.cursor = scope.cursor->NextSibling();
            }
            if (scope.cursor == nullptr) { return nullptr; }
            XmlElement* element = static_cast<XmlElement*>(scope.cursor);
            scope.cursor = scope.cursor->NextSibling();
            return element;
        }

        // --- scalar text <-> value ---
        [[nodiscard]] static StringView ScalarTag(ScalarKind kind)
        {
            switch (kind)
            {
                case ScalarKind::Bool:    return StringView(u8"bool");
                case ScalarKind::Int8:    return StringView(u8"i8");
                case ScalarKind::UInt8:   return StringView(u8"u8");
                case ScalarKind::Int16:   return StringView(u8"i16");
                case ScalarKind::UInt16:  return StringView(u8"u16");
                case ScalarKind::Int32:   return StringView(u8"i32");
                case ScalarKind::UInt32:  return StringView(u8"u32");
                case ScalarKind::Int64:   return StringView(u8"i64");
                case ScalarKind::UInt64:  return StringView(u8"u64");
                case ScalarKind::Float32: return StringView(u8"f32");
                case ScalarKind::Float64: return StringView(u8"f64");
            }
            return StringView(u8"scalar");
        }

        static void WriteScalar(const void* p, ScalarKind kind, String& out)
        {
            switch (kind)
            {
                case ScalarKind::Bool:    out.Append(*static_cast<const bool*>(p) ? StringView(u8"true") : StringView(u8"false")); break;
                case ScalarKind::Int8:    AppendValue(out, static_cast<i64>(*static_cast<const i8*>(p))); break;
                case ScalarKind::UInt8:   AppendValue(out, static_cast<u64>(*static_cast<const u8*>(p))); break;
                case ScalarKind::Int16:   AppendValue(out, static_cast<i64>(*static_cast<const i16*>(p))); break;
                case ScalarKind::UInt16:  AppendValue(out, static_cast<u64>(*static_cast<const u16*>(p))); break;
                case ScalarKind::Int32:   AppendValue(out, *static_cast<const i32*>(p)); break;
                case ScalarKind::UInt32:  AppendValue(out, *static_cast<const u32*>(p)); break;
                case ScalarKind::Int64:   AppendValue(out, *static_cast<const i64*>(p)); break;
                case ScalarKind::UInt64:  AppendValue(out, *static_cast<const u64*>(p)); break;
                case ScalarKind::Float32: AppendValue(out, *static_cast<const f32*>(p)); break;
                case ScalarKind::Float64: AppendValue(out, *static_cast<const f64*>(p)); break;
            }
        }

        static bool ReadScalar(void* p, ScalarKind kind, const String& text)
        {
            const char* s = reinterpret_cast<const char*>(text.CStr());
            switch (kind)
            {
                case ScalarKind::Bool:
                {
                    if (text == StringView(u8"true") || text == StringView(u8"1")) { *static_cast<bool*>(p) = true; return true; }
                    if (text == StringView(u8"false") || text == StringView(u8"0")) { *static_cast<bool*>(p) = false; return true; }
                    return false;
                }
                case ScalarKind::Int8:    { i64 v = 0; if (!ParseI64(s, v)) return false; *static_cast<i8*>(p)  = static_cast<i8>(v);  return true; }
                case ScalarKind::Int16:   { i64 v = 0; if (!ParseI64(s, v)) return false; *static_cast<i16*>(p) = static_cast<i16>(v); return true; }
                case ScalarKind::Int32:   { i64 v = 0; if (!ParseI64(s, v)) return false; *static_cast<i32*>(p) = static_cast<i32>(v); return true; }
                case ScalarKind::Int64:   { i64 v = 0; if (!ParseI64(s, v)) return false; *static_cast<i64*>(p) = v; return true; }
                case ScalarKind::UInt8:   { u64 v = 0; if (!ParseU64C(s, v)) return false; *static_cast<u8*>(p)  = static_cast<u8>(v);  return true; }
                case ScalarKind::UInt16:  { u64 v = 0; if (!ParseU64C(s, v)) return false; *static_cast<u16*>(p) = static_cast<u16>(v); return true; }
                case ScalarKind::UInt32:  { u64 v = 0; if (!ParseU64C(s, v)) return false; *static_cast<u32*>(p) = static_cast<u32>(v); return true; }
                case ScalarKind::UInt64:  { u64 v = 0; if (!ParseU64C(s, v)) return false; *static_cast<u64*>(p) = v; return true; }
                case ScalarKind::Float32: { char* end = nullptr; *static_cast<f32*>(p) = std::strtof(s, &end); return end != s; }
                case ScalarKind::Float64: { char* end = nullptr; *static_cast<f64*>(p) = std::strtod(s, &end); return end != s; }
            }
            return false;
        }

        static bool ParseI64(const char* s, i64& out)
        {
            char* end = nullptr;
            out = static_cast<i64>(std::strtoll(s, &end, 10));
            return end != s;
        }
        static bool ParseU64C(const char* s, u64& out)
        {
            char* end = nullptr;
            out = static_cast<u64>(std::strtoull(s, &end, 10));
            return end != s;
        }
        static bool ParseU64(StringView text, u64& out)
        {
            String t(text);
            return ParseU64C(reinterpret_cast<const char*>(t.CStr()), out);
        }

        static void EncodeHex(const u8* data, usize size, String& out)
        {
            static const char kHex[] = "0123456789abcdef";
            for (usize i = 0; i < size; ++i)
            {
                out.PushBack(static_cast<utf8char>(kHex[data[i] >> 4]));
                out.PushBack(static_cast<utf8char>(kHex[data[i] & 0xF]));
            }
        }
        static bool DecodeHex(const String& hex, u8* data, usize size)
        {
            if (hex.Size() != size * 2) { return false; }
            auto nibble = [](utf8char c) -> int {
                if (c >= u8'0' && c <= u8'9') return c - u8'0';
                if (c >= u8'a' && c <= u8'f') return c - u8'a' + 10;
                if (c >= u8'A' && c <= u8'F') return c - u8'A' + 10;
                return -1;
            };
            for (usize i = 0; i < size; ++i)
            {
                const int hi = nibble(hex[i * 2]);
                const int lo = nibble(hex[i * 2 + 1]);
                if (hi < 0 || lo < 0) { return false; }
                data[i] = static_cast<u8>((hi << 4) | lo);
            }
            return true;
        }

        XmlDocument m_doc;                 // owned (write mode)
        XmlElement* m_writeCurrent = nullptr;
        Array<XmlElement*> m_writeStack;
        Array<ReadScope> m_readStack;      // read mode
        const char* m_pendingKey = nullptr;
    };
}
