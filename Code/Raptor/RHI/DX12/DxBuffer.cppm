/// DX12 implementation of Buffer.
/// Ported from Sedulous.RHI.DX12/DX12Buffer.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:buffer;

import raptor.core;
import raptor.rhi;
import :conversions;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl; // forward

class DxBufferImpl : public Buffer {
public:
    Status init(ID3D12Device* device, const BufferDesc& d) {
        desc = d;

        auto heapType = toHeapType(d.memory);
        auto flags    = toBufferResourceFlags(d.usage);

        // Initial state based on heap type.
        state_ = D3D12_RESOURCE_STATE_COMMON;
        if (heapType == D3D12_HEAP_TYPE_UPLOAD)   state_ = D3D12_RESOURCE_STATE_GENERIC_READ;
        if (heapType == D3D12_HEAP_TYPE_READBACK)  state_ = D3D12_RESOURCE_STATE_COPY_DEST;

        u64 alignedSize = (d.size + 255) & ~u64(255); // 256-byte alignment for CBVs

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = heapType;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = alignedSize;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = flags;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &rd, state_, nullptr,
            IID_PPV_ARGS(&resource_));
        if (FAILED(hr)) {
            logErrorf("DxBuffer: CreateCommittedResource failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        // Persistently map upload/readback buffers.
        if (heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK)
            resource_->Map(0, nullptr, &persistentMap_);

        return ErrorCode::Ok;
    }

    void* map() override {
        if (persistentMap_) return persistentMap_;
        void* ptr = nullptr;
        if (SUCCEEDED(resource_->Map(0, nullptr, &ptr))) return ptr;
        return nullptr;
    }

    void unmap() override {
        if (persistentMap_) return; // don't unmap persistently mapped buffers
        resource_->Unmap(0, nullptr);
    }

    void cleanup() {
        if (persistentMap_) { resource_->Unmap(0, nullptr); persistentMap_ = nullptr; }
        resource_.Reset();
    }

    // ---- Internal ----
    [[nodiscard]] ID3D12Resource*       handle() const { return resource_.Get(); }
    [[nodiscard]] D3D12_RESOURCE_STATES currentState() const { return state_; }
    void setState(D3D12_RESOURCE_STATES s) { state_ = s; }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return resource_ ? resource_->GetGPUVirtualAddress() : 0; }

private:
    ComPtr<ID3D12Resource>  resource_;
    D3D12_RESOURCE_STATES   state_ = D3D12_RESOURCE_STATE_COMMON;
    void*                   persistentMap_ = nullptr;
};

} // namespace raptor::rhi::dx12
