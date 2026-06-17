/// DX12 implementation of AccelStruct.
/// Wraps a D3D12 buffer resource in RAYTRACING_ACCELERATION_STRUCTURE state.
/// Ported from Sedulous.RHI.DX12/DX12AccelStruct.bf.

module;

#include "DxIncludes.h"

export module raptor.rhi.dx12:accel_struct;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::dx12 {

class DxAccelStructImpl : public AccelStruct {
public:
    Status init(ID3D12Device* device, const AccelStructDesc& d) {
        type_ = d.type;

        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = 256 * 1024; // 256 KB default, grown at build time
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(&resource_));
        if (FAILED(hr)) return ErrorCode::Unknown;

        gpuAddr_ = resource_->GetGPUVirtualAddress();
        return ErrorCode::Ok;
    }

    AccelStructType type()          const override { return type_; }
    u64             deviceAddress() const override { return gpuAddr_; }

    void cleanup() { resource_.Reset(); }

    [[nodiscard]] ID3D12Resource* handle() const { return resource_.Get(); }

private:
    AccelStructType        type_ = AccelStructType::BottomLevel;
    u64                    gpuAddr_ = 0;
    ComPtr<ID3D12Resource> resource_;
};

} // namespace raptor::rhi::dx12
