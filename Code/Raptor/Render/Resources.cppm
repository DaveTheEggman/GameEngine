/// Raptor::Render — the `:resources` partition.
///
/// GPU resource primitives shared by the renderers. First up: `DynamicUniformRing`, a
/// frames-in-flight ring of dynamic-offset uniform slots — the §8 replacement for the slice's
/// grow-the-buffer object UBO. The buffer is partitioned into `framesInFlight` equal regions;
/// each frame writes ONLY its own region (selected by the device ring index), so the CPU never
/// overwrites data the GPU is still reading for a frame in flight. Per-object data is written
/// into 256-byte-aligned slots and bound with a dynamic offset.
///
/// The ring grows by reallocating (rare — only when scene complexity exceeds the current
/// per-frame capacity; steady state never grows), draining the GPU first so no in-flight frame
/// references the old buffer. Each (re)allocation bumps a generation so a consumer can rebuild
/// the bind group it created over `Buffer()`.

module;
#include "Core/Prelude.h"

export module raptor.render:resources;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

class DynamicUniformRing {
public:
    // `slotSize` is the per-allocation stride (>= the largest struct, 256-aligned for dynamic
    // offsets). `framesInFlight` is the device ring depth (>= 1).
    DynamicUniformRing(rhi::Device& device, u32 framesInFlight, u64 slotSize = 256) noexcept
        : m_device(&device), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight), m_slotSize(slotSize) {}

    ~DynamicUniformRing() { Release(); }

    DynamicUniformRing(const DynamicUniformRing&) = delete;
    DynamicUniformRing& operator=(const DynamicUniformRing&) = delete;

    // Ensure each frame region holds at least `slotsPerFrame` slots. Grows by reallocating
    // (draining the GPU first); never shrinks. Returns false if the buffer can't be created.
    bool Reserve(u32 slotsPerFrame) {
        if (slotsPerFrame <= m_slotsPerFrame && m_buffer != nullptr) { return true; }
        if (slotsPerFrame == 0) { slotsPerFrame = 1; }

        m_device->WaitIdle();   // an in-flight frame may still reference the old buffer
        Release();

        rhi::BufferDesc bd{};
        bd.size   = static_cast<u64>(m_framesInFlight) * static_cast<u64>(slotsPerFrame) * m_slotSize;
        bd.usage  = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryLocation::CpuToGpu;
        bd.label  = u8"uniform.ring";
        if (!m_device->CreateBuffer(bd, m_buffer).IsOk()) { m_buffer = nullptr; return false; }

        m_slotsPerFrame = slotsPerFrame;
        ++m_generation;
        return true;
    }

    // Begin a frame: select the device ring slot's region + map it for writes. `frameIndex`
    // is the device ring index (0..framesInFlight-1).
    void BeginFrame(u32 frameIndex) {
        m_frameBase = static_cast<u32>(frameIndex % m_framesInFlight) * m_slotsPerFrame;
        m_cursor    = 0;
        m_mapped    = (m_buffer != nullptr) ? static_cast<u8*>(m_buffer->Map()) : nullptr;
    }

    struct Slot { u32 dynamicOffset = 0; void* ptr = nullptr; bool ok = false; };

    // Allocate one slot in this frame's region. Returns ok=false if the region is exhausted
    // (the caller Reserve'd too few — never silently grows mid-frame, which would move offsets).
    [[nodiscard]] Slot Allocate() {
        if (m_mapped == nullptr || m_cursor >= m_slotsPerFrame) { return Slot{}; }
        const u32 slot = m_frameBase + m_cursor;
        ++m_cursor;
        const u64 offset = static_cast<u64>(slot) * m_slotSize;
        return Slot{ static_cast<u32>(offset), m_mapped + offset, true };
    }

    void EndFrame() {
        if (m_mapped != nullptr && m_buffer != nullptr) { m_buffer->Unmap(); }
        m_mapped = nullptr;
    }

    [[nodiscard]] rhi::Buffer* Buffer()     const noexcept { return m_buffer; }
    [[nodiscard]] u64          SlotSize()   const noexcept { return m_slotSize; }
    [[nodiscard]] u32          Generation() const noexcept { return m_generation; }   // bumps on realloc

private:
    void Release() {
        if (m_buffer != nullptr) { m_device->DestroyBuffer(m_buffer); m_buffer = nullptr; }
        m_mapped = nullptr;
    }

    rhi::Device* m_device;
    rhi::Buffer* m_buffer = nullptr;
    u8*          m_mapped = nullptr;
    u32          m_framesInFlight;
    u64          m_slotSize;
    u32          m_slotsPerFrame = 0;
    u32          m_frameBase     = 0;
    u32          m_cursor        = 0;
    u32          m_generation    = 0;
};

} // namespace raptor::render
