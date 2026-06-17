/// DX12 implementation of TextureView.
/// Lazily creates SRV/RTV/DSV/UAV CPU descriptor handles on demand.
/// Ported from Sedulous.RHI.DX12/DX12TextureView.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:texture_view;

import raptor.core;
import raptor.rhi;
import :conversions;
import :texture;
import :descriptor_heap;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxTextureViewImpl : public TextureView {
public:
    Status init(ID3D12Device* device, DxTextureImpl* tex, const TextureViewDesc& d,
                DxDescriptorHeapAllocator* srvHeap, DxDescriptorHeapAllocator* rtvHeap,
                DxDescriptorHeapAllocator* dsvHeap) {
        device_  = device;
        texture_ = tex;
        viewDesc_= d;
        srvHeap_ = srvHeap;
        rtvHeap_ = rtvHeap;
        dsvHeap_ = dsvHeap;
        return ErrorCode::Ok;
    }

    // ---- Lazy SRV ----
    D3D12_CPU_DESCRIPTOR_HANDLE getSrv() {
        if (hasSrv_) return srv_;

        auto fmt = (viewDesc_.format == TextureFormat::Undefined) ? texture_->desc.format : viewDesc_.format;
        DXGI_FORMAT srvFmt = isDepthFormat(fmt) ? toDepthSrvFormat(fmt) : toDxgiFormat(fmt);

        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = srvFmt;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        u32 mips = viewDesc_.mipLevelCount;
        if (mips == 0) mips = texture_->desc.mipLevelCount - viewDesc_.baseMipLevel;

        switch (viewDesc_.dimension) {
        case TextureViewDimension::Texture1D:
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            sd.Texture1D.MostDetailedMip = viewDesc_.baseMipLevel;
            sd.Texture1D.MipLevels = mips;
            break;
        case TextureViewDimension::Texture1DArray:
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            sd.Texture1DArray.MostDetailedMip = viewDesc_.baseMipLevel;
            sd.Texture1DArray.MipLevels = mips;
            sd.Texture1DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
            sd.Texture1DArray.ArraySize = viewDesc_.arrayLayerCount;
            break;
        case TextureViewDimension::Texture2D:
            if (texture_->desc.sampleCount > 1) {
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            } else {
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip = viewDesc_.baseMipLevel;
                sd.Texture2D.MipLevels = mips;
            }
            break;
        case TextureViewDimension::Texture2DArray:
            if (texture_->desc.sampleCount > 1) {
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
                sd.Texture2DMSArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                sd.Texture2DMSArray.ArraySize = viewDesc_.arrayLayerCount;
            } else {
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                sd.Texture2DArray.MostDetailedMip = viewDesc_.baseMipLevel;
                sd.Texture2DArray.MipLevels = mips;
                sd.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                sd.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
            }
            break;
        case TextureViewDimension::TextureCube:
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            sd.TextureCube.MostDetailedMip = viewDesc_.baseMipLevel;
            sd.TextureCube.MipLevels = mips;
            break;
        case TextureViewDimension::TextureCubeArray:
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            sd.TextureCubeArray.MostDetailedMip = viewDesc_.baseMipLevel;
            sd.TextureCubeArray.MipLevels = mips;
            sd.TextureCubeArray.First2DArrayFace = viewDesc_.baseArrayLayer;
            sd.TextureCubeArray.NumCubes = viewDesc_.arrayLayerCount / 6;
            break;
        case TextureViewDimension::Texture3D:
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            sd.Texture3D.MostDetailedMip = viewDesc_.baseMipLevel;
            sd.Texture3D.MipLevels = mips;
            break;
        }

        srv_ = srvHeap_->allocate();
        device_->CreateShaderResourceView(texture_->handle(), &sd, srv_);
        hasSrv_ = true;
        return srv_;
    }

    // ---- Lazy RTV ----
    D3D12_CPU_DESCRIPTOR_HANDLE getRtv() {
        if (hasRtv_) return rtv_;

        auto fmt = (viewDesc_.format == TextureFormat::Undefined) ? texture_->desc.format : viewDesc_.format;
        bool isArray = texture_->desc.arrayLayerCount > 1;

        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = toDxgiFormat(fmt);

        if (viewDesc_.dimension == TextureViewDimension::Texture2D) {
            if (isArray) {
                if (texture_->desc.sampleCount > 1) {
                    rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                    rd.Texture2DMSArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                    rd.Texture2DMSArray.ArraySize = viewDesc_.arrayLayerCount;
                } else {
                    rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rd.Texture2DArray.MipSlice = viewDesc_.baseMipLevel;
                    rd.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                    rd.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
                }
            } else if (texture_->desc.sampleCount > 1) {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            } else {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rd.Texture2D.MipSlice = viewDesc_.baseMipLevel;
            }
        } else if (viewDesc_.dimension == TextureViewDimension::Texture3D) {
            rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            rd.Texture3D.MipSlice = viewDesc_.baseMipLevel;
            rd.Texture3D.WSize = viewDesc_.arrayLayerCount;
        } else {
            // 2DArray, Cube, CubeArray
            if (texture_->desc.sampleCount > 1) {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                rd.Texture2DMSArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                rd.Texture2DMSArray.ArraySize = viewDesc_.arrayLayerCount;
            } else {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rd.Texture2DArray.MipSlice = viewDesc_.baseMipLevel;
                rd.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                rd.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
            }
        }

        rtv_ = rtvHeap_->allocate();
        device_->CreateRenderTargetView(texture_->handle(), &rd, rtv_);
        hasRtv_ = true;
        return rtv_;
    }

    // ---- Lazy DSV ----
    D3D12_CPU_DESCRIPTOR_HANDLE getDsv() {
        if (hasDsv_) return dsv_;

        auto fmt = (viewDesc_.format == TextureFormat::Undefined) ? texture_->desc.format : viewDesc_.format;
        bool isArray = texture_->desc.arrayLayerCount > 1;

        D3D12_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = toDxgiFormat(fmt);

        if (viewDesc_.dimension == TextureViewDimension::Texture2D) {
            if (isArray) {
                if (texture_->desc.sampleCount > 1) {
                    dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
                    dd.Texture2DMSArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                    dd.Texture2DMSArray.ArraySize = viewDesc_.arrayLayerCount;
                } else {
                    dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dd.Texture2DArray.MipSlice = viewDesc_.baseMipLevel;
                    dd.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                    dd.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
                }
            } else if (texture_->desc.sampleCount > 1) {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            } else {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dd.Texture2D.MipSlice = viewDesc_.baseMipLevel;
            }
        } else {
            if (texture_->desc.sampleCount > 1) {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
                dd.Texture2DMSArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                dd.Texture2DMSArray.ArraySize = viewDesc_.arrayLayerCount;
            } else {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dd.Texture2DArray.MipSlice = viewDesc_.baseMipLevel;
                dd.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
                dd.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
            }
        }

        dsv_ = dsvHeap_->allocate();
        device_->CreateDepthStencilView(texture_->handle(), &dd, dsv_);
        hasDsv_ = true;
        return dsv_;
    }

    // ---- Lazy UAV ----
    D3D12_CPU_DESCRIPTOR_HANDLE getUav() {
        if (hasUav_) return uav_;

        auto fmt = (viewDesc_.format == TextureFormat::Undefined) ? texture_->desc.format : viewDesc_.format;

        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = toDxgiFormat(fmt);

        switch (viewDesc_.dimension) {
        case TextureViewDimension::Texture1D:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            ud.Texture1D.MipSlice = viewDesc_.baseMipLevel;
            break;
        case TextureViewDimension::Texture1DArray:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            ud.Texture1DArray.MipSlice = viewDesc_.baseMipLevel;
            ud.Texture1DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
            ud.Texture1DArray.ArraySize = viewDesc_.arrayLayerCount;
            break;
        case TextureViewDimension::Texture2D:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            ud.Texture2D.MipSlice = viewDesc_.baseMipLevel;
            break;
        case TextureViewDimension::Texture2DArray:
        case TextureViewDimension::TextureCube:
        case TextureViewDimension::TextureCubeArray:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            ud.Texture2DArray.MipSlice = viewDesc_.baseMipLevel;
            ud.Texture2DArray.FirstArraySlice = viewDesc_.baseArrayLayer;
            ud.Texture2DArray.ArraySize = viewDesc_.arrayLayerCount;
            break;
        case TextureViewDimension::Texture3D:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            ud.Texture3D.MipSlice = viewDesc_.baseMipLevel;
            ud.Texture3D.FirstWSlice = viewDesc_.baseArrayLayer;
            ud.Texture3D.WSize = viewDesc_.arrayLayerCount;
            break;
        default:
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            ud.Texture2D.MipSlice = viewDesc_.baseMipLevel;
            break;
        }

        uav_ = srvHeap_->allocate();
        device_->CreateUnorderedAccessView(texture_->handle(), nullptr, &ud, uav_);
        hasUav_ = true;
        return uav_;
    }

    void cleanup() {
        if (hasSrv_) { srvHeap_->free(srv_); hasSrv_ = false; }
        if (hasRtv_) { rtvHeap_->free(rtv_); hasRtv_ = false; }
        if (hasDsv_) { dsvHeap_->free(dsv_); hasDsv_ = false; }
        if (hasUav_) { srvHeap_->free(uav_); hasUav_ = false; }
    }

    // ---- Internal ----
    [[nodiscard]] DxTextureImpl*    dxTexture() const { return texture_; }
    [[nodiscard]] TextureViewDesc   viewDesc()  const { return viewDesc_; }
    [[nodiscard]] TextureFormat     format()    const {
        return (viewDesc_.format == TextureFormat::Undefined) ? texture_->desc.format : viewDesc_.format;
    }
    [[nodiscard]] u32 width()  const { return texture_->desc.width;  }
    [[nodiscard]] u32 height() const { return texture_->desc.height; }

private:
    ID3D12Device*               device_   = nullptr;
    DxTextureImpl*              texture_  = nullptr;
    TextureViewDesc             viewDesc_{};
    DxDescriptorHeapAllocator*  srvHeap_  = nullptr;
    DxDescriptorHeapAllocator*  rtvHeap_  = nullptr;
    DxDescriptorHeapAllocator*  dsvHeap_  = nullptr;

    D3D12_CPU_DESCRIPTOR_HANDLE srv_{}, rtv_{}, dsv_{}, uav_{};
    bool hasSrv_ = false, hasRtv_ = false, hasDsv_ = false, hasUav_ = false;
};

} // namespace raptor::rhi::dx12
