/// DX12 implementation of Texture.
/// Ported from Sedulous.RHI.DX12/DX12Texture.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <algorithm>
#include <cstring>

export module raptor.rhi.dx12:texture;

import raptor.core;
import raptor.rhi;
import :conversions;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxTextureImpl : public Texture {
public:
    Status init(ID3D12Device* device, const TextureDesc& d) {
        desc = d;

        DXGI_FORMAT format = isDepthFormat(d.format)
            ? toTypelessDepthFormat(d.format)
            : toDxgiFormat(d.format);

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = toResourceDimension(d.dimension);
        rd.Width            = static_cast<UINT64>(d.width);
        rd.Height           = d.height;
        rd.DepthOrArraySize = static_cast<UINT16>((d.dimension == TextureDimension::Texture3D) ? d.depth : d.arrayLayerCount);
        rd.MipLevels        = static_cast<UINT16>(d.mipLevelCount);
        rd.Format           = format;
        rd.SampleDesc       = { d.sampleCount, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = toTextureResourceFlags(d.usage);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        state_ = D3D12_RESOURCE_STATE_COMMON;

        D3D12_CLEAR_VALUE clearVal{};
        D3D12_CLEAR_VALUE* pClearVal = nullptr;

        if (static_cast<u32>(d.usage & TextureUsage::DepthStencil)) {
            clearVal.Format = toDxgiFormat(d.format);
            clearVal.DepthStencil = { 1.0f, 0 };
            pClearVal = &clearVal;
            state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        } else if (static_cast<u32>(d.usage & TextureUsage::RenderTarget)) {
            clearVal.Format = format;
            clearVal.Color[0] = 0; clearVal.Color[1] = 0; clearVal.Color[2] = 0; clearVal.Color[3] = 1;
            pClearVal = &clearVal;
            state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &rd, state_, pClearVal,
            IID_PPV_ARGS(&resource_));
        if (FAILED(hr)) {
            logErrorf("DxTexture: CreateCommittedResource failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }
        ownsResource_ = true;
        return ErrorCode::Ok;
    }

    /// Initialize from an existing resource (e.g. swap chain buffer). Does not own.
    void initFromExisting(ID3D12Resource* resource, const TextureDesc& d) {
        resource_.Attach(resource);
        resource_->AddRef(); // ComPtr will Release — balance it
        desc = d;
        ownsResource_ = false;
        state_ = D3D12_RESOURCE_STATE_PRESENT;
    }

    void cleanup() {
        subresourceStates_.Clear();
        resource_.Reset();
    }

    // ---- Internal ----
    [[nodiscard]] ID3D12Resource*       handle() const { return resource_.Get(); }

    [[nodiscard]] D3D12_RESOURCE_STATES currentState() const { return state_; }
    void setState(D3D12_RESOURCE_STATES s) { state_ = s; subresourceStates_.Clear(); }

    [[nodiscard]] D3D12_RESOURCE_STATES getSubresourceState(u32 mip, u32 layer) const {
        if (subresourceStates_.IsEmpty()) return state_;
        u32 idx = mip + layer * desc.mipLevelCount;
        return (idx < subresourceStates_.Size()) ? subresourceStates_[idx] : state_;
    }

    void setSubresourceState(u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount, D3D12_RESOURCE_STATES s) {
        u32 totalMips   = desc.mipLevelCount;
        u32 totalLayers = std::max((desc.dimension == TextureDimension::Texture3D) ? desc.depth : desc.arrayLayerCount, 1u);
        u32 mipEnd   = (mipCount   == ~0u) ? totalMips   : std::min(baseMip + mipCount, totalMips);
        u32 layerEnd = (layerCount == ~0u) ? totalLayers : std::min(baseLayer + layerCount, totalLayers);

        // All subresources? Collapse to uniform.
        if (baseMip == 0 && mipEnd >= totalMips && baseLayer == 0 && layerEnd >= totalLayers) {
            state_ = s;
            subresourceStates_.Clear();
            return;
        }
        // Promote to per-subresource.
        if (subresourceStates_.IsEmpty()) {
            if (s == state_) return;
            subresourceStates_.Resize(totalMips * totalLayers, state_);
        }
        for (u32 l = baseLayer; l < layerEnd; ++l)
            for (u32 m = baseMip; m < mipEnd; ++m)
                subresourceStates_[m + l * totalMips] = s;

        // Try to collapse.
        auto first = subresourceStates_[0];
        for (usize i = 1; i < subresourceStates_.Size(); ++i)
            if (subresourceStates_[i] != first) return;
        state_ = first;
        subresourceStates_.Clear();
    }

private:
    ComPtr<ID3D12Resource>                resource_;
    D3D12_RESOURCE_STATES                 state_ = D3D12_RESOURCE_STATE_COMMON;
    Array<D3D12_RESOURCE_STATES>           subresourceStates_;
    bool                                  ownsResource_ = true;
};

} // namespace raptor::rhi::dx12
