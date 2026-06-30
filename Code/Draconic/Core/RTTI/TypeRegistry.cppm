// Draconic Core — :type_registry partition
//
// Explicit type registration; lookup by id or qualified name.

module;
#include "Core/Prelude.h"

export module draconic.core:type_registry;

import :base;
import :type_info;
import :array;
import :hash_map;

export namespace draconic::core
{
    // =======================================================================
    // Type registry — explicit registration; lookup by id or qualified name.
    // =======================================================================
    class TypeRegistry
    {
    public:
        void Register(const TypeInfo& info)
        {
            if (m_byId.Contains(info.id))
            {
                return; // already registered
            }
            m_byId.InsertOrAssign(info.id, &info);
            m_all.PushBack(&info);
        }

        [[nodiscard]] const TypeInfo* FindById(TypeId id) const noexcept
        {
            const TypeInfo* const* found = m_byId.Find(id);
            return (found != nullptr) ? *found : nullptr;
        }

        [[nodiscard]] const TypeInfo* FindByName(const char* namespaceName, const char* name) const noexcept
        {
            return FindById(ComputeTypeId(namespaceName, name));
        }

        [[nodiscard]] usize Count() const noexcept { return m_all.Size(); }
        [[nodiscard]] const Array<const TypeInfo*>& All() const noexcept { return m_all; }

    private:
        HashMap<TypeId, const TypeInfo*> m_byId;
        Array<const TypeInfo*> m_all;
    };

    [[nodiscard]] TypeRegistry& GlobalTypeRegistry() noexcept
    {
        static TypeRegistry instance;
        return instance;
    }
}
