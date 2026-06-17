/// DX12 implementation of Sampler.
/// Ported from Sedulous.RHI.DX12/DX12Sampler.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:sampler;

import raptor.core;
import raptor.rhi;
import :conversions;
import :descriptor_heap;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxSamplerImpl : public Sampler {
public:
    Status init(ID3D12Device* device, const SamplerDesc& d, DxDescriptorHeapAllocator* samplerHeap) {
        samplerHeap_ = samplerHeap;

        bool isComparison = d.compare.HasValue();

        D3D12_SAMPLER_DESC sd{};
        if (isComparison)
            sd.Filter = toFilter(d.minFilter, d.magFilter, d.mipmapFilter, true);
        else if (d.maxAnisotropy > 1)
            sd.Filter = D3D12_FILTER_ANISOTROPIC;
        else
            sd.Filter = toFilter(d.minFilter, d.magFilter, d.mipmapFilter, false);

        sd.AddressU = toAddressMode(d.addressU);
        sd.AddressV = toAddressMode(d.addressV);
        sd.AddressW = toAddressMode(d.addressW);
        sd.MipLODBias     = d.mipLodBias;
        sd.MaxAnisotropy  = static_cast<UINT>(d.maxAnisotropy);
        sd.ComparisonFunc = isComparison ? toComparisonFunc(*d.compare) : D3D12_COMPARISON_FUNC_NEVER;
        sd.MinLOD = d.minLod;
        sd.MaxLOD = d.maxLod;

        switch (d.borderColor) {
        case SamplerBorderColor::TransparentBlack: sd.BorderColor[0]=0; sd.BorderColor[1]=0; sd.BorderColor[2]=0; sd.BorderColor[3]=0; break;
        case SamplerBorderColor::OpaqueBlack:      sd.BorderColor[0]=0; sd.BorderColor[1]=0; sd.BorderColor[2]=0; sd.BorderColor[3]=1; break;
        case SamplerBorderColor::OpaqueWhite:      sd.BorderColor[0]=1; sd.BorderColor[1]=1; sd.BorderColor[2]=1; sd.BorderColor[3]=1; break;
        }

        handle_ = samplerHeap->allocate();
        device->CreateSampler(&sd, handle_);
        return ErrorCode::Ok;
    }

    void cleanup() {
        if (samplerHeap_) samplerHeap_->free(handle_);
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE handle() const { return handle_; }

private:
    D3D12_CPU_DESCRIPTOR_HANDLE handle_{};
    DxDescriptorHeapAllocator*  samplerHeap_ = nullptr;
};

} // namespace raptor::rhi::dx12
