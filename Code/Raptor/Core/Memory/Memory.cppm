// Raptor Core — :memory partition
//
// Allocator interface, alignment / raw-memory utilities, and concrete
// allocators. Allocation is explicit everywhere: containers take an IAllocator;
// nothing here allocates behind your back.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <atomic>       // allocation tracking counters
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
    // Placement construct / destroy
    //   Centralizing placement-new here (where <new> is included) keeps the
    //   global placement operator new reachable: container modules call these
    //   instead of `::new`, so consumers that instantiate containers never need
    //   to include <new> themselves. (GCC modules require it at the
    //   instantiation site otherwise.)
    // =======================================================================
    template <typename T, typename... Args>
    T* Construct(void* where, Args&&... args)
    {
        return ::new (where) T(Forward<Args>(args)...);
    }

    template <typename T>
    void Destruct(T* object) noexcept
    {
        object->~T();
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

    // =======================================================================
    // TrackingAllocator — wraps another allocator and counts live allocations
    // for leak detection. Thread-safe (atomic counters). Tracks counts, not
    // bytes (which would need a per-allocation header).
    // =======================================================================
    class TrackingAllocator final : public IAllocator
    {
    public:
        explicit TrackingAllocator(IAllocator& backing) noexcept : m_backing(&backing) {}

        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            void* pointer = m_backing->Allocate(size, alignment);
            if (pointer != nullptr)
            {
                m_liveCount.fetch_add(1, std::memory_order_relaxed);
                m_totalAllocations.fetch_add(1, std::memory_order_relaxed);
            }
            return pointer;
        }

        void Free(void* pointer) override
        {
            if (pointer != nullptr)
            {
                m_backing->Free(pointer);
                m_liveCount.fetch_sub(1, std::memory_order_relaxed);
                m_totalFrees.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] u64 LiveAllocations() const noexcept { return m_liveCount.load(std::memory_order_relaxed); }
        [[nodiscard]] u64 TotalAllocations() const noexcept { return m_totalAllocations.load(std::memory_order_relaxed); }
        [[nodiscard]] u64 TotalFrees() const noexcept { return m_totalFrees.load(std::memory_order_relaxed); }
        [[nodiscard]] bool HasLeaks() const noexcept { return LiveAllocations() != 0; }

    private:
        IAllocator* m_backing;
        std::atomic<u64> m_liveCount{ 0 };
        std::atomic<u64> m_totalAllocations{ 0 };
        std::atomic<u64> m_totalFrees{ 0 };
    };

    // =======================================================================
    // PoolAllocator — fixed-size block allocator over a caller buffer.
    //   O(1) allocate/free via an intrusive free list. Allocations must fit in
    //   the block size; returns nullptr when exhausted.
    // =======================================================================
    class PoolAllocator final : public IAllocator
    {
    public:
        PoolAllocator() noexcept = default;

        PoolAllocator(void* buffer, usize bufferSize, usize blockSize, usize blockAlign = kDefaultAlignment) noexcept
        {
            Init(buffer, bufferSize, blockSize, blockAlign);
        }

        void Init(void* buffer, usize bufferSize, usize blockSize, usize blockAlign = kDefaultAlignment) noexcept
        {
            RAPTOR_ASSERT(IsPowerOfTwo(blockAlign));

            // Each free block stores a next-pointer, so blocks are at least pointer-sized.
            usize actualBlock = (blockSize < sizeof(void*)) ? sizeof(void*) : blockSize;
            actualBlock = AlignUp(actualBlock, blockAlign);
            m_blockSize = actualBlock;

            const usize alignedStart = AlignUp(reinterpret_cast<usize>(buffer), blockAlign);
            byte* cursor = reinterpret_cast<byte*>(alignedStart);
            byte* end = static_cast<byte*>(buffer) + bufferSize;

            m_freeList = nullptr;
            m_blockCount = 0;
            m_freeCount = 0;
            while (cursor + actualBlock <= end)
            {
                *reinterpret_cast<void**>(cursor) = m_freeList;
                m_freeList = cursor;
                ++m_blockCount;
                ++m_freeCount;
                cursor += actualBlock;
            }
        }

        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            RAPTOR_ASSERT_MSG(size <= m_blockSize, "PoolAllocator allocation exceeds block size");
            (void)alignment;
            if (m_freeList == nullptr)
            {
                return nullptr;
            }
            void* block = m_freeList;
            m_freeList = *reinterpret_cast<void**>(m_freeList);
            --m_freeCount;
            return block;
        }

        void Free(void* pointer) override
        {
            if (pointer == nullptr)
            {
                return;
            }
            *reinterpret_cast<void**>(pointer) = m_freeList;
            m_freeList = pointer;
            ++m_freeCount;
        }

        [[nodiscard]] usize BlockSize() const noexcept { return m_blockSize; }
        [[nodiscard]] usize Capacity() const noexcept { return m_blockCount; }
        [[nodiscard]] usize FreeCount() const noexcept { return m_freeCount; }

    private:
        void* m_freeList = nullptr;
        usize m_blockSize = 0;
        usize m_blockCount = 0;
        usize m_freeCount = 0;
    };

    // =======================================================================
    // StackAllocator — LIFO bump allocator with markers. Free is a no-op;
    // reclaim back to a saved marker (or Reset to reclaim everything).
    // =======================================================================
    class StackAllocator final : public IAllocator
    {
    public:
        using Marker = usize;

        StackAllocator() noexcept = default;
        StackAllocator(void* buffer, usize size) noexcept { Init(buffer, size); }

        void Init(void* buffer, usize size) noexcept
        {
            m_begin = static_cast<byte*>(buffer);
            m_current = m_begin;
            m_end = m_begin + size;
        }

        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            RAPTOR_ASSERT(IsPowerOfTwo(alignment));
            const usize aligned = AlignUp(reinterpret_cast<usize>(m_current), alignment);
            byte* result = reinterpret_cast<byte*>(aligned);
            if (result + size > m_end)
            {
                return nullptr;
            }
            m_current = result + size;
            return result;
        }

        void Free(void* /*pointer*/) override {}

        [[nodiscard]] Marker GetMarker() const noexcept { return static_cast<Marker>(m_current - m_begin); }
        void FreeToMarker(Marker marker) noexcept { m_current = m_begin + marker; }
        void Reset() noexcept { m_current = m_begin; }

        [[nodiscard]] usize Used() const noexcept { return static_cast<usize>(m_current - m_begin); }
        [[nodiscard]] usize Capacity() const noexcept { return static_cast<usize>(m_end - m_begin); }

    private:
        byte* m_begin = nullptr;
        byte* m_current = nullptr;
        byte* m_end = nullptr;
    };

    // =======================================================================
    // FrameAllocator — double-buffered linear allocator. Splits a buffer in
    // two; allocations come from the current half. NextFrame() swaps halves and
    // resets the new current, so a frame's allocations stay valid through the
    // following frame (transient cross-frame data). Free is a no-op.
    // =======================================================================
    class FrameAllocator final : public IAllocator
    {
    public:
        FrameAllocator() noexcept = default;
        FrameAllocator(void* buffer, usize size) noexcept { Init(buffer, size); }

        void Init(void* buffer, usize size) noexcept
        {
            const usize half = size / 2;
            byte* bytes = static_cast<byte*>(buffer);
            m_buffers[0].Init(bytes, half);
            m_buffers[1].Init(bytes + half, size - half);
            m_current = 0;
        }

        [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) override
        {
            return m_buffers[m_current].Allocate(size, alignment);
        }

        void Free(void* /*pointer*/) override {}

        // Advances to the next frame: the other half becomes current and is reset.
        void NextFrame() noexcept
        {
            m_current ^= 1u;
            m_buffers[m_current].Reset();
        }

        [[nodiscard]] usize Used() const noexcept { return m_buffers[m_current].Used(); }
        [[nodiscard]] usize Capacity() const noexcept { return m_buffers[m_current].Capacity(); }

    private:
        LinearAllocator m_buffers[2];
        u32 m_current = 0;
    };
}
