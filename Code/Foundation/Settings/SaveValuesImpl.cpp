// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Settings - SaveValues' type definition and its payload.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.settings;

import foundation.core;

using namespace foundation::core;

namespace foundation::settings
{
    RTTI_DEFINE_OBJECT_VERSIONED(SaveValues, "rtti::foundation::settings", 1)

    namespace
    {
        // Kinds are written by name, so a save file reads in a text editor and a kind added later
        // does not renumber the others.
        constexpr StringView kKindNames[] = {u8"bool", u8"int", u8"float", u8"text"};

        bool KindFromName(StringView name, SaveValueKind& out) noexcept
        {
            for (usize i = 0; i < ArrayCount(kKindNames); ++i)
            {
                if (kKindNames[i] == name)
                {
                    out = static_cast<SaveValueKind>(i);
                    return true;
                }
            }
            return false;
        }
    }

    void SaveValues::Serialize(ISerializer& ar)
    {
        u32 count = static_cast<u32>(m_entries.Size());
        ar.Key("values");
        ar.BeginArray(count);
        if (ar.Mode() == SerializeMode::Read)
        {
            m_entries.Clear();
            m_entries.Reserve(count);
        }
        for (u32 i = 0; i < count; ++i)
        {
            Entry entry;
            if (ar.Mode() == SerializeMode::Write)
            {
                entry = m_entries[i];
            }
            String kind(kKindNames[static_cast<usize>(entry.kind)]);
            ar.BeginObject();
            foundation::core::Serialize(ar, "key", entry.key);
            foundation::core::Serialize(ar, "kind", kind);
            if (ar.Mode() == SerializeMode::Read && !KindFromName(kind.AsView(), entry.kind))
            {
                ar.FailPayload(ErrorCode::InvalidArgument); // a kind this build does not know
                ar.EndObject();
                break;
            }
            switch (entry.kind)
            {
            case SaveValueKind::Bool:
                foundation::core::Serialize(ar, "value", entry.boolValue);
                break;
            case SaveValueKind::Int:
                foundation::core::Serialize(ar, "value", entry.intValue);
                break;
            case SaveValueKind::Float:
                foundation::core::Serialize(ar, "value", entry.floatValue);
                break;
            case SaveValueKind::Text:
                foundation::core::Serialize(ar, "value", entry.textValue);
                break;
            }
            ar.EndObject();
            if (ar.Mode() == SerializeMode::Read)
            {
                // A file edited by hand may be out of order or repeat a key: insert in order, the
                // later value winning.
                const usize at = LowerBound(entry.key.AsView());
                if (at < m_entries.Size() && m_entries[at].key == entry.key)
                {
                    m_entries[at] = Move(entry);
                }
                else
                {
                    m_entries.Insert(at, Move(entry));
                }
            }
        }
        ar.EndArray();
    }

    void RegisterSaveValuesType()
    {
        static const bool once = []()
        {
            GlobalTypeRegistry().Register(SaveValues::StaticType());
            RegisterSerializable<SaveValues>();
            return true;
        }();
        (void)once;
    }
}
