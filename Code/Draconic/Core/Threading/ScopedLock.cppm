// Draconic Core - :scoped_lock partition
//
// Generic RAII lock guard for any type with Lock()/Unlock().

module;
#include "Core/Prelude.h"

export module draconic.core:scoped_lock;

export namespace draconic::core
{
    template <typename Lockable>
    class ScopedLock
    {
    public:
        explicit ScopedLock(Lockable& lockable) noexcept : m_lockable(&lockable)
        {
            m_lockable->Lock();
        }
        ~ScopedLock() { m_lockable->Unlock(); }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        Lockable* m_lockable;
    };
}
