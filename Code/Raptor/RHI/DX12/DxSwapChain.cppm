/// DX12 implementation of SwapChain.
/// Wraps IDXGISwapChain3 for presentation.
/// Ported from Sedulous.RHI.DX12/DX12SwapChain.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:swap_chain;

import raptor.core;
import raptor.rhi;
import :conversions;
import :surface;
import :texture;
import :texture_view;
import :descriptor_heap;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl; // forward
class DxQueueImpl;  // forward

class DxSwapChainImpl : public SwapChain {
public:
    Status init(ID3D12Device* device, IDXGIFactory4* factory, ID3D12CommandQueue* gfxQueue,
                DxSurfaceImpl* surface, const SwapChainDesc& d,
                DxDescriptorHeapAllocator* srvHeap, DxDescriptorHeapAllocator* rtvHeap,
                DxDescriptorHeapAllocator* dsvHeap) {
        d3dDevice_   = device;
        format_      = d.format;
        width_       = d.width;
        height_      = d.height;
        bufferCount_ = d.bufferCount;
        presentMode_ = d.presentMode;
        srvHeap_     = srvHeap;
        rtvHeap_     = rtvHeap;
        dsvHeap_     = dsvHeap;

        DXGI_FORMAT swapFmt = stripSrgb(toDxgiFormat(d.format));

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width       = d.width;
        sd.Height      = d.height;
        sd.Format      = swapFmt;
        sd.SampleDesc  = { 1, 0 };
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = d.bufferCount;
        sd.Scaling     = DXGI_SCALING_STRETCH;
        sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
        if (d.presentMode == PresentMode::Immediate)
            sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        ComPtr<IDXGISwapChain1> sc1;
        HRESULT hr = factory->CreateSwapChainForHwnd(
            gfxQueue, surface->handle(), &sd, nullptr, nullptr, &sc1);
        if (FAILED(hr)) {
            logErrorf("DxSwapChain: CreateSwapChainForHwnd failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        factory->MakeWindowAssociation(surface->handle(), DXGI_MWA_NO_ALT_ENTER);

        hr = sc1.As(&swapChain_);
        if (FAILED(hr)) return ErrorCode::Unknown;

        if (acquireBackBuffers() != ErrorCode::Ok) return ErrorCode::Unknown;
        currentIndex_ = swapChain_->GetCurrentBackBufferIndex();
        return ErrorCode::Ok;
    }

    // ---- SwapChain interface ----
    TextureFormat format()            const override { return format_; }
    u32           width()             const override { return width_; }
    u32           height()            const override { return height_; }
    u32           bufferCount()       const override { return bufferCount_; }
    u32           currentImageIndex() const override { return currentIndex_; }
    Texture*      currentTexture()    override { return (currentIndex_ < textures_.Size()) ? textures_[currentIndex_] : nullptr; }
    TextureView*  currentTextureView()override { return (currentIndex_ < views_.Size())    ? views_[currentIndex_]    : nullptr; }

    Status acquireNextImage() override {
        currentIndex_ = swapChain_->GetCurrentBackBufferIndex();
        return ErrorCode::Ok;
    }

    Status present(Queue* /*queue*/) override {
        UINT syncInterval = 1, flags = 0;
        switch (presentMode_) {
        case PresentMode::Immediate: syncInterval = 0; flags = DXGI_PRESENT_ALLOW_TEARING; break;
        case PresentMode::Mailbox:   syncInterval = 0; break;
        case PresentMode::Fifo:      syncInterval = 1; break;
        case PresentMode::FifoRelaxed: syncInterval = 1; break;
        }
        return SUCCEEDED(swapChain_->Present(syncInterval, flags)) ? ErrorCode::Ok : ErrorCode::Unknown;
    }

    Status resize(u32 w, u32 h) override {
        if (w == 0 || h == 0) return ErrorCode::Ok;
        width_ = w; height_ = h;
        releaseBackBuffers();
        HRESULT hr = swapChain_->ResizeBuffers(bufferCount_, w, h,
            stripSrgb(toDxgiFormat(format_)), 0);
        if (FAILED(hr)) return ErrorCode::Unknown;
        if (acquireBackBuffers() != ErrorCode::Ok) return ErrorCode::Unknown;
        currentIndex_ = swapChain_->GetCurrentBackBufferIndex();
        return ErrorCode::Ok;
    }

    void cleanup() {
        releaseBackBuffers();
        swapChain_.Reset();
    }

private:
    Status acquireBackBuffers() {
        for (u32 i = 0; i < bufferCount_; ++i) {
            ID3D12Resource* resource = nullptr;
            if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&resource)))) return ErrorCode::Unknown;

            auto* tex = new DxTextureImpl();
            TextureDesc td{}; td.dimension = TextureDimension::Texture2D; td.format = format_;
            td.width = width_; td.height = height_; td.arrayLayerCount = 1; td.mipLevelCount = 1;
            td.sampleCount = 1; td.usage = TextureUsage::RenderTarget;
            tex->initFromExisting(resource, td);
            resource->Release(); // initFromExisting AddRef'd
            textures_.PushBack(tex);

            auto* view = new DxTextureViewImpl();
            TextureViewDesc vd{}; vd.format = format_; vd.dimension = TextureViewDimension::Texture2D;
            vd.mipLevelCount = 1; vd.arrayLayerCount = 1;
            view->init(d3dDevice_, tex, vd, srvHeap_, rtvHeap_, dsvHeap_);
            views_.PushBack(view);
        }
        return ErrorCode::Ok;
    }

    void releaseBackBuffers() {
        for (auto* v : views_)   { v->cleanup(); delete v; }
        views_.Clear();
        for (auto* t : textures_) { t->cleanup(); delete t; }
        textures_.Clear();
    }

    ComPtr<IDXGISwapChain3>          swapChain_;
    ID3D12Device*                    d3dDevice_  = nullptr;
    TextureFormat                    format_     = TextureFormat::RGBA8UnormSrgb;
    u32                              width_ = 0, height_ = 0, bufferCount_ = 2;
    u32                              currentIndex_ = 0;
    PresentMode                      presentMode_ = PresentMode::Fifo;
    Array<DxTextureImpl*>            textures_;
    Array<DxTextureViewImpl*>        views_;
    DxDescriptorHeapAllocator*       srvHeap_ = nullptr;
    DxDescriptorHeapAllocator*       rtvHeap_ = nullptr;
    DxDescriptorHeapAllocator*       dsvHeap_ = nullptr;
};

} // namespace raptor::rhi::dx12
