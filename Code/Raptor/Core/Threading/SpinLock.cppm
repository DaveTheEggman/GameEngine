// Raptor Core — :spin_lock partition
//
// Busy-wait exclusive lock for very short critical sections.

module;
#include "Core/Prelude.h"
#include <atomic>

export module raptor.core:spin_lock;


export namespace raptor::core
{
    class SpinLock
    {
    public:
        void Lock() noexcept
        {
            while (m_locked.exchange(true, std::memory_order_acquire))
            {
                while (m_locked.load(std::memory_order_relaxed)) {} // spin without RMW
            }
        }

        [[nodiscard]] bool TryLock() noexcept
        {
            return !m_locked.exchange(true, std::memory_order_acquire);
        }

        void Unlock() noexcept { m_locked.store(false, std::memory_order_release); }

    private:
        std::atomic<bool> m_locked{ false };
    };
}
