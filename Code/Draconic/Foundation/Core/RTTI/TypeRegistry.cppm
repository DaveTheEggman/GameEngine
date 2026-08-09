// Draconic Core - :type_registry partition
//
// Explicit type registration; lookup by id or qualified name.

module;
#include "Core/Prelude.h"

export module foundation.core:type_registry;

import :base;
import :type_info;
import :array;
import :hash_map;
import :string;
import :string_hash;

export namespace foundation::core
{
    // The availability domain a type was registered under - an OPEN set identified by
    // name hash, not an enum: core owns only the DEFAULT domain "Runtime" (what a shipped
    // player registers); other layers tag registrations with their own names (the editor
    // passes TypeDomain(u8"Editor")). A type's domain answers "which processes have this
    // type" - tooling reads it (e.g. the script API browser marks non-runtime bindings);
    // runtime behavior never depends on it.
    class TypeDomain
    {
    public:
        constexpr TypeDomain() noexcept = default; // empty ("no domain"); registry-internal
        constexpr explicit TypeDomain(StringView name) noexcept : m_name(name) {}

        [[nodiscard]] constexpr StringHash Name() const noexcept { return m_name; }
        [[nodiscard]] constexpr bool operator==(const TypeDomain&) const noexcept = default;

    private:
        StringHash m_name;
    };

    inline constexpr TypeDomain kRuntimeTypeDomain{StringView(u8"Runtime")};

    // =======================================================================
    // Type registry - explicit registration; lookup by id or qualified name.
    // =======================================================================
    class TypeRegistry
    {
    public:
        void Register(const TypeInfo& info, TypeDomain domain = kRuntimeTypeDomain)
        {
            if (m_byId.Contains(info.id))
            {
                // Already registered: the domain may only WIDEN to Runtime (registration
                // order is arbitrary - if any path the player takes registers the type,
                // "the player has it" is the truth). Never narrows.
                if (domain == kRuntimeTypeDomain)
                {
                    m_domains.Remove(info.id); // absent = Runtime
                }
                return;
            }
            m_byId.InsertOrAssign(info.id, &info);
            m_all.PushBack(&info);
            if (!(domain == kRuntimeTypeDomain))
            {
                m_domains.InsertOrAssign(info.id, domain);
            }
        }

        // The domain `id` was registered under; unknown ids (and unregistered types)
        // read as Runtime - the default is the absence of a tag.
        [[nodiscard]] TypeDomain DomainOf(TypeId id) const noexcept
        {
            const TypeDomain* found = m_domains.Find(id);
            return (found != nullptr) ? *found : kRuntimeTypeDomain;
        }

        [[nodiscard]] const TypeInfo* FindById(TypeId id) const noexcept
        {
            const TypeInfo* const* found = m_byId.Find(id);
            return (found != nullptr) ? *found : nullptr;
        }

        [[nodiscard]] const TypeInfo* FindByName(const char* namespaceName,
                                                 const char* name) const noexcept
        {
            if (const TypeInfo* found = FindById(ComputeTypeId(namespaceName, name)))
            {
                return found;
            }
            return FindByLegacyName(namespaceName, name);
        }

        // LEGACY-NAME COMPATIBILITY (2026-08 debrand): serialized data (content envelopes,
        // settings sections, scenes) stores qualified type names, and the debrand renamed
        // every registration namespace: Foundation "draconic::X" -> "rtti::X" and Engine
        // "draconic::X" -> "rtti::engine::X". Old files must keep resolving, so a miss on
        // a "draconic::"-prefixed namespace retries both current spellings. Data converges
        // to the new names on its next save; remove this once no pre-debrand files matter.
        [[nodiscard]] const TypeInfo* FindByLegacyName(const char* namespaceName,
                                                       const char* name) const noexcept
        {
            constexpr usize kLegacyLength = 8; // strlen("draconic")
            if (namespaceName == nullptr)
            {
                return nullptr;
            }
            for (usize i = 0; i < kLegacyLength; ++i)
            {
                if (namespaceName[i] != "draconic"[i])
                {
                    return nullptr; // not a legacy-prefixed namespace
                }
            }
            const char* rest = namespaceName + kLegacyLength; // "" or "::rest"
            if (rest[0] != '\0' && !(rest[0] == ':' && rest[1] == ':'))
            {
                return nullptr; // e.g. "draconicish::x" - not ours
            }
            // "draconic[::rest]" -> "rtti[::rest]" (Foundation), then
            // "rtti::engine[::rest]" (Engine subsystems - their C++ namespace moved too).
            char remapped[256];
            if (const TypeInfo* found =
                    FindById(ComputeTypeId(ComposeNamespace(remapped, "rtti", rest), name)))
            {
                return found;
            }
            return FindById(ComputeTypeId(ComposeNamespace(remapped, "rtti::engine", rest), name));
        }

    private:
        // Concatenate prefix+rest into `buffer` (256 bytes; overflow truncates to the prefix
        // alone, which simply misses the lookup - legacy namespaces are all far shorter).
        [[nodiscard]] static const char* ComposeNamespace(char (&buffer)[256], const char* prefix,
                                                          const char* rest) noexcept
        {
            usize n = 0;
            for (; prefix[n] != '\0' && n < 255; ++n)
            {
                buffer[n] = prefix[n];
            }
            for (usize i = 0; rest[i] != '\0' && n < 255; ++i, ++n)
            {
                buffer[n] = rest[i];
            }
            buffer[n] = '\0';
            return buffer;
        }

    public:

        [[nodiscard]] usize Count() const noexcept { return m_all.Size(); }
        [[nodiscard]] const Array<const TypeInfo*>& All() const noexcept { return m_all; }

    private:
        HashMap<TypeId, const TypeInfo*> m_byId;
        Array<const TypeInfo*> m_all;
        HashMap<TypeId, TypeDomain> m_domains; // sparse: only non-Runtime entries
    };

    [[nodiscard]] TypeRegistry& GlobalTypeRegistry() noexcept
    {
        static TypeRegistry instance;
        return instance;
    }
}
