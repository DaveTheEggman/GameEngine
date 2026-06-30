module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:hash_set;

import :base;
import :allocator;
import :hash;
import :hash_map;

export namespace draconic::core
{
    // =======================================================================
    // HashSet — a set of keys, built on HashMap. Iterates keys.
    // =======================================================================
    template <typename K, typename Hasher = Hash<K>>
    class HashSet
    {
        using MapType = HashMap<K, u8, Hasher>;

    public:
        HashSet() noexcept = default;
        explicit HashSet(IAllocator& allocator) noexcept : m_map(allocator) {}

        // Returns true if the key was newly inserted, false if already present.
        bool Insert(const K& key)
        {
            const bool existed = m_map.Contains(key);
            m_map.InsertOrAssign(key, u8{ 0 });
            return !existed;
        }

        [[nodiscard]] bool Contains(const K& key) const noexcept { return m_map.Contains(key); }
        bool Remove(const K& key) noexcept { return m_map.Remove(key); }
        void Clear() noexcept { m_map.Clear(); }

        [[nodiscard]] usize Size() const noexcept { return m_map.Size(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_map.IsEmpty(); }

        // Iteration yields keys.
        template <typename MapIterator>
        class BasicIterator
        {
        public:
            explicit BasicIterator(MapIterator it) noexcept : m_it(it) {}
            [[nodiscard]] const K& operator*() const noexcept { return (*m_it).key; }
            BasicIterator& operator++() noexcept { ++m_it; return *this; }
            [[nodiscard]] bool operator!=(const BasicIterator& other) const noexcept { return m_it != other.m_it; }

        private:
            MapIterator m_it;
        };

        [[nodiscard]] auto begin() noexcept { return BasicIterator{ m_map.begin() }; }
        [[nodiscard]] auto end() noexcept { return BasicIterator{ m_map.end() }; }
        [[nodiscard]] auto begin() const noexcept { return BasicIterator{ m_map.begin() }; }
        [[nodiscard]] auto end() const noexcept { return BasicIterator{ m_map.end() }; }

    private:
        MapType m_map;
    };
}
