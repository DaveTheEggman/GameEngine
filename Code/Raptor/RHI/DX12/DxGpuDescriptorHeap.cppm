/// GPU-visible descriptor heap with contiguous block allocation.
/// Used for CBV/SRV/UAV and Sampler heaps that are shader-visible.
/// Ported from Sedulous.RHI.DX12/DX12GpuDescriptorHeap.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:gpu_descriptor_heap;

import raptor.core;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxGpuDescriptorHeap {
public:
    DxGpuDescriptorHeap() = default;

    Status init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 capacity,
                bool shaderVisible = true) {
        capacity_ = capacity;

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type           = type;
        hd.NumDescriptors = capacity;
        hd.Flags          = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                          : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_));
        if (FAILED(hr)) return ErrorCode::Unknown;

        cpuStart_      = heap_->GetCPUDescriptorHandleForHeapStart();
        if (shaderVisible)
            gpuStart_  = heap_->GetGPUDescriptorHandleForHeapStart();
        incrementSize_ = device->GetDescriptorHandleIncrementSize(type);
        return ErrorCode::Ok;
    }

    /// Allocate a contiguous block. Returns offset or -1.
    i32 allocate(u32 count) {
        if (count == 0) return -1;
        // First-fit from free list.
        for (usize i = 0; i < freeBlocks_.Size(); ++i) {
            auto& b = freeBlocks_[i];
            if (b.count >= count) {
                u32 off = b.offset;
                if (b.count == count)
                    freeBlocks_.RemoveAt(i);
                else
                    b = { b.offset + count, b.count - count };
                return static_cast<i32>(off);
            }
        }
        // Bump allocate.
        if (nextFree_ + count <= capacity_) {
            u32 off = nextFree_;
            nextFree_ += count;
            return static_cast<i32>(off);
        }
        return -1;
    }

    /// Free a block with coalescing.
    void free(u32 offset, u32 count) {
        if (count == 0) return;
        u32 mOff = offset, mCnt = count;
        for (usize i = 0; i < freeBlocks_.Size(); ) {
            if (freeBlocks_[i].offset + freeBlocks_[i].count == mOff) {
                mOff = freeBlocks_[i].offset; mCnt += freeBlocks_[i].count;
                freeBlocks_.RemoveAt(i);
            } else if (mOff + mCnt == freeBlocks_[i].offset) {
                mCnt += freeBlocks_[i].count;
                freeBlocks_.RemoveAt(i);
            } else ++i;
        }
        if (mOff + mCnt == nextFree_)
            nextFree_ = mOff;
        else
            freeBlocks_.PushBack({ mOff, mCnt });
    }

    D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(u32 offset) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h{}; h.ptr = cpuStart_.ptr + static_cast<SIZE_T>(offset) * incrementSize_; return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(u32 offset) const {
        D3D12_GPU_DESCRIPTOR_HANDLE h{}; h.ptr = gpuStart_.ptr + static_cast<UINT64>(offset) * incrementSize_; return h;
    }

    void Destroy() { heap_.Reset(); freeBlocks_.Clear(); }

    [[nodiscard]] ID3D12DescriptorHeap* heap()          const { return heap_.Get(); }
    [[nodiscard]] u32                   incrementSize()  const { return incrementSize_; }

private:
    struct FreeBlock { u32 offset; u32 count; };

    ComPtr<ID3D12DescriptorHeap>  heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE   cpuStart_{};
    D3D12_GPU_DESCRIPTOR_HANDLE   gpuStart_{};
    u32                           incrementSize_ = 0;
    u32                           capacity_  = 0;
    u32                           nextFree_  = 0;
    Array<FreeBlock>              freeBlocks_;
};

} // namespace raptor::rhi::dx12
