// Core - :type_registry partition
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
    // name hash, not an enum: core owns only the DEFAULT domain "Runtime" (what the player
    // runtime registers); other layers tag registrations with their own names (the editor
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

        // LEGACY-NAME COMPATIBILITY: serialized data (content envelopes, settings sections,
        // scenes) stores qualified type names. Older files use legacy namespace spellings that
        // remap to the current ones: Foundation "draconic::X" -> "rtti::X", Engine
        // "draconic::X" -> "rtti::engine::X", and asset-cook types (which moved from the Editor
        // collection to the Pipeline collection) "draconic::editor::<lib>" and
        // "rtti::editor::<lib>" -> "rtti::pipeline::<lib>". Old files must keep resolving, so a
        // miss on a legacy namespace retries the current spellings. Data converges to the new
        // names on its next save; this mapping can be removed once no legacy files matter.
        [[nodiscard]] const TypeInfo* FindByLegacyName(const char* namespaceName,
                                                       const char* name) const noexcept
        {
            if (namespaceName == nullptr)
            {
                return nullptr;
            }
            char remapped[256];
            // Legacy "draconic[::rest]" root -> "rtti[::rest]" (Foundation) or
            // "rtti::engine[::rest]" (Engine subsystems, whose C++ namespace differs too).
            if (StartsWith(namespaceName, "draconic"))
            {
                const char* rest = namespaceName + 8; // "" or "::rest"
                if (rest[0] != '\0' && !(rest[0] == ':' && rest[1] == ':'))
                {
                    return nullptr; // e.g. "draconicish::x" - not ours
                }
                if (const TypeInfo* found =
                        FindById(ComputeTypeId(ComposeNamespace(remapped, "rtti", rest), name)))
                {
                    return found;
                }
                if (const TypeInfo* found = FindById(
                        ComputeTypeId(ComposeNamespace(remapped, "rtti::engine", rest), name)))
                {
                    return found;
                }
                // Legacy "draconic::<lib>" asset/cook type -> "rtti::pipeline::<lib>". These
                // identities use the subsystem-flavored spelling ("draconic::physics"::
                // CollisionShapeAsset, "draconic::script"::ScriptClassAsset), not an
                // editor-collection spelling; the types live under the Pipeline collection.
                if (const TypeInfo* found = FindById(
                        ComputeTypeId(ComposeNamespace(remapped, "rtti::pipeline", rest), name)))
                {
                    return found;
                }
                if (StartsWith(rest, "::editor"))
                {
                    // "draconic::editor::<lib>" asset-cook type that MOVED -> "rtti::pipeline::<lib>".
                    if (const TypeInfo* found = FindById(ComputeTypeId(
                            ComposeNamespace(remapped, "rtti::pipeline", rest + 8 /*"::editor"*/),
                            name)))
                    {
                        return found;
                    }
                    // Still-editor type: the current spelling carries the per-collection prefix,
                    // so "draconic::editor[::rest]" -> "rtti::editor::editor[::rest]" (e.g. the
                    // RecentProjectsSettings settings section).
                    return FindById(ComputeTypeId(
                        ComposeNamespace(remapped, "rtti::editor::editor", rest + 8), name));
                }
                return nullptr;
            }
            // "rtti::editor::<lib>": an asset-cook type spelling from before it moved to the
            // Pipeline collection -> "rtti::pipeline::<lib>". Then the still-editor respelling
            // ("rtti::editor[::rest]" -> "rtti::editor::editor[::rest]") for files written before
            // the per-collection prefix existed.
            if (StartsWith(namespaceName, "rtti::editor"))
            {
                const char* rest = namespaceName + 12; /*"rtti::editor"*/
                if (const TypeInfo* found = FindById(
                        ComputeTypeId(ComposeNamespace(remapped, "rtti::pipeline", rest), name)))
                {
                    return found;
                }
                return FindById(ComputeTypeId(
                    ComposeNamespace(remapped, "rtti::editor::editor", rest), name));
            }
            return nullptr;
        }

        // Prefix test: does `s` begin with `prefix`?
        [[nodiscard]] static bool StartsWith(const char* s, const char* prefix) noexcept
        {
            for (usize i = 0; prefix[i] != '\0'; ++i)
            {
                if (s[i] != prefix[i])
                {
                    return false;
                }
            }
            return true;
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
