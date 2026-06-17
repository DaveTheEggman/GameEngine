/// Bump-allocating staging region within a GPU-visible descriptor heap.
/// Copies bind group descriptors from CPU heap into GPU heap at bind time.
/// Ported from Sedulous.RHI.DX12/DX12DescriptorStaging.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <algorithm>

export module raptor.rhi.dx12:descriptor_staging;

import raptor.core;
import :gpu_descriptor_heap;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDescriptorStaging {
public:
    DxDescriptorStaging() = default;

    void init(DxGpuDescriptorHeap* cpuHeap, DxGpuDescriptorHeap* gpuHeap,
              ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, u32 initialCapacity) {
        cpuHeap_  = cpuHeap;
        gpuHeap_  = gpuHeap;
        device_   = device;
        heapType_ = heapType;
        capacity_ = initialCapacity;
    }

    /// Copies `count` descriptors from `srcOffset` in CPU heap into GPU staging.
    /// Returns the staging offset in the GPU heap, or -1 on failure.
    i32 copyFrom(u32 srcOffset, u32 count) {
        if (count == 0) return -1;

        // Lazy allocation.
        if (blockOffset_ < 0) {
            blockOffset_ = gpuHeap_->allocate(capacity_);
            if (blockOffset_ < 0) return -1;
            current_ = 0;
        }

        // Grow if needed: retire current block, allocate bigger.
        if (current_ + count > capacity_) {
            u32 newCap = std::max(capacity_ * 2, current_ + count);
            i32 newBlock = gpuHeap_->allocate(newCap);
            if (newBlock < 0) return -1;
            retiredBlocks_.PushBack({ blockOffset_, capacity_ });
            blockOffset_ = newBlock;
            capacity_ = newCap;
            current_ = 0;
        }

        u32 dstOffset = static_cast<u32>(blockOffset_) + current_;
        device_->CopyDescriptorsSimple(count,
            gpuHeap_->getCpuHandle(dstOffset),
            cpuHeap_->getCpuHandle(srcOffset),
            heapType_);
        current_ += count;
        return static_cast<i32>(dstOffset);
    }

    /// Resets bump pointer. Called when command pool resets after fence wait.
    void reset() {
        current_ = 0;
        for (auto& b : retiredBlocks_)
            gpuHeap_->free(static_cast<u32>(b.offset), b.capacity);
        retiredBlocks_.Clear();
    }

    /// Frees all blocks.
    void destroy() {
        if (blockOffset_ >= 0) {
            gpuHeap_->free(static_cast<u32>(blockOffset_), capacity_);
            blockOffset_ = -1;
        }
        for (auto& b : retiredBlocks_)
            gpuHeap_->free(static_cast<u32>(b.offset), b.capacity);
        retiredBlocks_.Clear();
    }

private:
    struct RetiredBlock { i32 offset; u32 capacity; };

    DxGpuDescriptorHeap*         cpuHeap_  = nullptr;
    DxGpuDescriptorHeap*         gpuHeap_  = nullptr;
    ID3D12Device*                device_   = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE   heapType_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    i32                          blockOffset_ = -1;
    u32                          capacity_ = 0;
    u32                          current_  = 0;
    Array<RetiredBlock>          retiredBlocks_;
};

} // namespace raptor::rhi::dx12
