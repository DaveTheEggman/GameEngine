// Draconic Core - :serializable_registry partition
//
// SerializableRegistry: maps a reflected TypeId to a factory that default-builds
// the concrete ISerializable. The polymorphic load path (content database) reads
// a stored type name, resolves the TypeInfo via the type registry, then asks
// this registry to create the object before running Serialize() on it.
//
// Registration is explicit (RegisterSerializable<T>()), matching the reflection
// registration convention - no static-init-order reliance.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.core:serializable_registry;

import :base;
import :type_info;
import :ref_counted;
import :allocator;
import :hash_map;
import :iserializable;

export namespace draconic::core
{
    using SerializableFactory = RefPtr<ISerializable> (*)();

    class SerializableRegistry
    {
    public:
        void Register(TypeId id, SerializableFactory factory)
        {
            m_factories.InsertOrAssign(id, factory);
        }

        // Default-builds the ISerializable for `id`, or null if unregistered.
        [[nodiscard]] RefPtr<ISerializable> Create(TypeId id) const
        {
            const SerializableFactory* factory = m_factories.Find(id);
            return (factory != nullptr) ? (*factory)() : RefPtr<ISerializable>{};
        }

        [[nodiscard]] bool Contains(TypeId id) const { return m_factories.Find(id) != nullptr; }

    private:
        HashMap<TypeId, SerializableFactory> m_factories;
    };

    [[nodiscard]] SerializableRegistry& GlobalSerializableRegistry() noexcept
    {
        static SerializableRegistry registry;
        return registry;
    }

    // Registers `T`'s default factory. Call once at startup (e.g. from a module's
    // RegisterTypes()). T must derive ISerializable and have a default ctor.
    template <typename T>
    void RegisterSerializable(SerializableRegistry& registry = GlobalSerializableRegistry())
    {
        registry.Register(T::StaticType().id,
                          []() -> RefPtr<ISerializable> { return MakeRef<T>(DefaultAllocator()); });
    }
}
