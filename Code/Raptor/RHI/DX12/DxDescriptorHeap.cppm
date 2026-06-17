/// Simple CPU-side descriptor heap allocator with free-list.
/// Ported from Sedulous.RHI.DX12/DX12DescriptorHeapAllocator.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:descriptor_heap;

import raptor.core;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDescriptorHeapAllocator {
public:
    DxDescriptorHeapAllocator() = default;

    Status init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 maxCount,
                D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE) {
        maxCount_ = maxCount;
        alive_.Resize(maxCount, static_cast<u8>(0));

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type           = type;
        hd.NumDescriptors = maxCount;
        hd.Flags          = flags;
        HRESULT hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_));
        if (FAILED(hr)) return ErrorCode::Unknown;

        heapStart_      = heap_->GetCPUDescriptorHandleForHeapStart();
        descriptorSize_ = device->GetDescriptorHandleIncrementSize(type);
        return ErrorCode::Ok;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE allocate() {
        for (u32 i = 0; i < maxCount_; ++i) {
            u32 idx = (searchStart_ + i) % maxCount_;
            if (!alive_[idx]) {
                alive_[idx] = true;
                ++allocCount_;
                searchStart_ = (idx + 1) % maxCount_;
                D3D12_CPU_DESCRIPTOR_HANDLE h{};
                h.ptr = heapStart_.ptr + static_cast<SIZE_T>(idx) * descriptorSize_;
                return h;
            }
        }
        return {}; // heap full
    }

    void free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr < heapStart_.ptr) return;
        u32 offset = static_cast<u32>((handle.ptr - heapStart_.ptr) / descriptorSize_);
        if (offset < maxCount_ && alive_[offset]) {
            alive_[offset] = false;
            --allocCount_;
        }
    }

    void destroy() {
        heap_.Reset();
        alive_.Clear();
    }

    [[nodiscard]] ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    [[nodiscard]] u32 descriptorSize() const { return descriptorSize_; }

private:
    ComPtr<ID3D12DescriptorHeap>  heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE   heapStart_{};
    u32                           descriptorSize_ = 0;
    u32                           maxCount_    = 0;
    u32                           allocCount_  = 0;
    u32                           searchStart_ = 0;
    Array<u8>                     alive_;
};

} // namespace raptor::rhi::dx12
