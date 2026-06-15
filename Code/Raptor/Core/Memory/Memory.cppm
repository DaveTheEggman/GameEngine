// Raptor Core — :memory partition
//
// Allocator interface, alignment / raw-memory utilities, and concrete
// allocators. Allocation is explicit everywhere: containers take an IAllocator;
// nothing here allocates behind your back.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <cstddef>      // std::max_align_t
#include <cstdlib>      // aligned_alloc / free
#include <cstring>      // memcpy / memmove / memset
#include <new>          // placement new

export module raptor.core:memory;

import :base;

export namespace raptor::core
{
    // =======================================================================
    // Alignment helpers
    // =======================================================================
    [[nodiscard]] constexpr bool IsPowerOfTwo(usize value) noexcept
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    [[nodiscard]] constexpr usize AlignUp(usize value, usize alignment) noexcept
    {
        // alignment must be a power of two.
        return (value + (alignment - 1)) & ~(alignment - 1);
    }

    [[nodiscard]] constexpr usize AlignDown(usize value, usize alignment) noexcept
    {
        return value & ~(alignment - 1);
    }

    [[nodiscard]] inline bool IsAligned(const void* pointer, usize alignment) noexcept
    {
        return (reinterpret_cast<usize>(pointer) & (alignment - 1)) == 0;
    }

    inline constexpr usize kDefaultAlignment = alignof(std::max_align_t);

    // =======================================================================
    // Raw memory operations (thin, named wrappers over the C library)
    // =======================================================================
    inline void* MemCopy(void* dst, const void* src, usize bytes) noexcept
    {
        return std::memcpy(dst, src, bytes);
    }

    inline void* MemMove(void* dst, const void* src, usize bytes) noexcept
    {
        return std::memmove(dst, src, bytes);
    }

    inline void* MemSet(void* dst, i32 value, usize bytes) noexcept
    {
        return std::memset(dst, value, bytes);
    }

    inline void MemZero(void* dst, usize bytes) noexcept
    {
        std::memset(dst, 0, bytes);
    }

    // =======================================================================
    // Allocator interface
    //   Allocate returns nullptr on failure (no exceptions).
    //   Free(nullptr) is a no-op.
    // =======================================================================
    class IAllocator
    {
    public:
        virtual ~IAllocator() = default;

        [[nodiscard]] virtual void* Allocate(usize size, usize alignment = kDefaultAlignment) = 0;
        virtual void Free(void* pointer) = 0;

        // Construct/destroy a single object through this allocator.
        template <typename T, typename... Args>
        [[nodiscard]] T* New(Args&&... args)
        {
            void* memory = Allocate(sizeof(T), alignof(T));
            if (memory == nullptr)
            {
                return nullptr;
            }
            return ::new (memory) T(Forward<Args>(args)...);
        }

        template <typename T>
        void Delete(T* pointer)
        {
            if (pointer == nullptr)
            {
                return;
            }
            pointer->~T();
            Free(pointer);
        }
    };

    // =======================================================================
    // SystemAllocator — aligned heap allocations from the OS/CRT.
    // =======================================================================
    class SystemAllocator final : public IAllocator
    {
    public:
        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            if (size == 0)
            {
                return nullptr;
            }

            RAPTOR_ASSERT(IsPowerOfTwo(alignment));

            // aligned_alloc requires the size to be a multiple of the alignment.
            const usize alignedSize = AlignUp(size, alignment);
#if RAPTOR_PLATFORM_WINDOWS
            return _aligned_malloc(alignedSize, alignment);
#else
            return std::aligned_alloc(alignment, alignedSize);
#endif
        }

        void Free(void* pointer) override
        {
#if RAPTOR_PLATFORM_WINDOWS
            _aligned_free(pointer);
#else
            std::free(pointer);
#endif
        }
    };

    // Process-wide default heap allocator.
    [[nodiscard]] IAllocator& DefaultAllocator() noexcept
    {
        static SystemAllocator instance;
        return instance;
    }

    // =======================================================================
    // LinearAllocator — bump-pointer arena over a caller-provided buffer.
    //   Individual frees are no-ops; reclaim all at once with Reset().
    //   Allocate returns nullptr when the arena is exhausted.
    // =======================================================================
    class LinearAllocator final : public IAllocator
    {
    public:
        LinearAllocator() noexcept = default;

        LinearAllocator(void* buffer, usize size) noexcept
        {
            Init(buffer, size);
        }

        void Init(void* buffer, usize size) noexcept
        {
            m_begin = static_cast<byte*>(buffer);
            m_current = m_begin;
            m_end = m_begin + size;
        }

        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            RAPTOR_ASSERT(IsPowerOfTwo(alignment));

            const usize current = reinterpret_cast<usize>(m_current);
            const usize aligned = AlignUp(current, alignment);
            byte* result = reinterpret_cast<byte*>(aligned);

            if (result + size > m_end)
            {
                return nullptr; // exhausted
            }

            m_current = result + size;
            return result;
        }

        // No-op: linear allocators reclaim in bulk via Reset().
        void Free(void* /*pointer*/) override {}

        void Reset() noexcept { m_current = m_begin; }

        [[nodiscard]] usize Used() const noexcept
        {
            return static_cast<usize>(m_current - m_begin);
        }

        [[nodiscard]] usize Capacity() const noexcept
        {
            return static_cast<usize>(m_end - m_begin);
        }

    private:
        byte* m_begin = nullptr;
        byte* m_current = nullptr;
        byte* m_end = nullptr;
    };
}
