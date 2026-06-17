/// DX12 implementation of Adapter.
/// Wraps IDXGIAdapter1, queries device features, creates DxDevice.
/// Ported from Sedulous.RHI.DX12/DX12Adapter.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <cstring>

export module raptor.rhi.dx12:adapter;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl; // forward

class DxAdapterImpl : public Adapter {
public:
    DxAdapterImpl(IDXGIAdapter1* adapter, IDXGIFactory4* factory)
        : adapter_(adapter), factory_(factory)
    {
        adapter_->GetDesc1(&desc_);
    }

    ~DxAdapterImpl() override {
        if (adapter_) { adapter_->Release(); adapter_ = nullptr; }
    }

    // ---- Adapter interface ----

    void getInfo(AdapterInfo& out) override {
        // DXGI Description is a WCHAR[] — construct String (wide) directly.
        out.name = String(reinterpret_cast<const widechar*>(desc_.Description));
        out.vendorId = desc_.VendorId;
        out.deviceId = desc_.DeviceId;
        out.type = (desc_.DedicatedVideoMemory > 0) ? AdapterType::DiscreteGpu : AdapterType::IntegratedGpu;
        out.supportedFeatures = buildFeatures();
    }

    DeviceFeatures buildFeatures() {
        // Create a temporary device to query features.
        ComPtr<ID3D12Device> tempDevice;
        HRESULT hr = D3D12CreateDevice(adapter_, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&tempDevice));
        if (FAILED(hr) || !tempDevice) return {};

        DeviceFeatures f{};

        // Check feature support.
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (SUCCEEDED(tempDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
            f.bindlessDescriptors = true; // DX12 always supports descriptor indexing
            f.timestampQueries = true;
            f.multiDrawIndirect = true;
            f.depthClamp = true;
            f.fillModeWireframe = true;
            f.textureCompressionBC = true;
            f.textureCompressionASTC = false;
            f.independentBlend = true;
            f.multiViewport = true;
            f.pipelineStatisticsQueries = true;
        }

        // Check mesh shader support.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        if (SUCCEEDED(tempDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
            f.meshShaders = (options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
        }

        // Check ray tracing support.
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        if (SUCCEEDED(tempDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
            f.rayTracing = (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
        }

        // Conservative limits for D3D12 feature level 12.0.
        f.maxBindGroups = 32;
        f.maxBindingsPerGroup = 1000000;
        f.maxPushConstantSize = 128;
        f.maxTextureDimension2D = 16384;
        f.maxTextureArrayLayers = 2048;
        f.maxComputeWorkgroupSizeX = 1024;
        f.maxComputeWorkgroupSizeY = 1024;
        f.maxComputeWorkgroupSizeZ = 64;
        f.maxComputeWorkgroupsPerDimension = 65535;
        f.maxBufferSize = static_cast<u64>(desc_.DedicatedVideoMemory);
        f.minUniformBufferOffsetAlignment = 256;
        f.minStorageBufferOffsetAlignment = 16;
        f.timestampPeriodNs = 1; // DX12 timestamps in ticks, period queried at runtime

        return f;
    }

    Status createDevice(const DeviceDesc& desc, Device*& out) override;

    // ---- Internal ----
    [[nodiscard]] IDXGIAdapter1*    handle()  const { return adapter_; }
    [[nodiscard]] IDXGIFactory4*    factory() const { return factory_; }
    [[nodiscard]] DXGI_ADAPTER_DESC1 adapterDesc() const { return desc_; }

private:
    IDXGIAdapter1*     adapter_ = nullptr; // owned, released in destructor
    IDXGIFactory4*     factory_ = nullptr; // not owned (Backend owns it)
    DXGI_ADAPTER_DESC1 desc_{};
};

} // namespace raptor::rhi::dx12
