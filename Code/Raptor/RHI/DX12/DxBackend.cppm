/// DX12 implementation of Backend.
/// Creates DXGI factory, enumerates adapters, creates surfaces.
/// Ported from Sedulous.RHI.DX12/DX12Backend.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <cstdio>

export module raptor.rhi.dx12:backend;

import raptor.core;
import raptor.rhi;
import :surface;
import :adapter;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

/// Configuration for DX12 backend creation.
struct DxBackendDesc {
    bool enableValidation = false;
};

/// DX12 implementation of Backend.
class DxBackendImpl : public Backend {
public:
    ~DxBackendImpl() override { destroyImpl(); }

    // ---- Backend interface ----

    Span<Adapter* const> enumerateAdapters() override {
        return Span<Adapter* const>(adapterPtrs_.Data(), adapterPtrs_.Size());
    }

    Status createSurface(void* windowHandle, void* /*displayHandle*/, Surface*& out) override {
        out = nullptr;
        if (!windowHandle) {
            logError("DxBackend: window handle is null");
            return ErrorCode::InvalidArgument;
        }
        out = new DxSurfaceImpl(reinterpret_cast<HWND>(windowHandle));
        return ErrorCode::Ok;
    }

    void destroy() override {
        destroyImpl();
        delete this;
    }

    // ---- Internal ----
    [[nodiscard]] IDXGIFactory4* factory() const { return factory_.Get(); }
    [[nodiscard]] bool validationEnabled() const { return validationEnabled_; }

private:
    friend Status createDxBackend(const DxBackendDesc& desc, Backend*& out);

    Status init(bool enableValidation) {
        validationEnabled_ = enableValidation;

        // Enable debug layer before device creation.
        if (validationEnabled_) {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
            }
        }

        // Create DXGI factory.
        UINT factoryFlags = validationEnabled_ ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) {
            logErrorf("DxBackend: CreateDXGIFactory2 failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        enumerateAdaptersInternal();
        isInitialized = true;
        return ErrorCode::Ok;
    }

    void enumerateAdaptersInternal() {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);

            // Skip software adapters.
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter.Reset();
                continue;
            }

            // Check D3D12 feature level 12.0 support.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
                auto* a = new DxAdapterImpl(adapter.Detach(), factory_.Get());
                adapters_.PushBack(a);
                adapterPtrs_.PushBack(a);
            }

            adapter.Reset();
        }

        // Expose adapters best-GPU-first; callers take [0]. See Backend::enumerateAdapters.
        sortAdaptersByPreference(adapterPtrs_);
    }

    void destroyImpl() {
        for (auto* a : adapters_) delete a;
        adapters_.Clear();
        adapterPtrs_.Clear();
        factory_.Reset();
    }

    ComPtr<IDXGIFactory4>         factory_;
    bool                          validationEnabled_ = false;
    Array<DxAdapterImpl*>         adapters_;
    Array<Adapter*>               adapterPtrs_;
};

/// Creates a DX12 backend. Caller owns the returned pointer — dispose via destroy().
[[nodiscard]] Status createDxBackend(const DxBackendDesc& desc, Backend*& out) {
    out = nullptr;
    auto* b = new DxBackendImpl();
    Status r = b->init(desc.enableValidation);
    if (r != ErrorCode::Ok) {
        delete b;
        return r;
    }
    out = b;
    return ErrorCode::Ok;
}

} // namespace raptor::rhi::dx12
