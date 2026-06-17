/// DX12 implementation of CommandPool.
/// Wraps an ID3D12CommandAllocator.
/// Ported from Sedulous.RHI.DX12/DX12CommandPool.bf.

module;

#include "DxIncludes.h"

#include <vector>

export module raptor.rhi.dx12:command_pool;

import raptor.core;
import raptor.rhi;
import :conversions;
import :command_buffer;
import :descriptor_staging;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl;          // forward
class DxCommandEncoderImpl;  // forward

class DxCommandPoolImpl : public CommandPool {
public:
    Status init(DxDeviceImpl* device, ID3D12Device* d3dDevice, QueueType queueType,
                DxGpuDescriptorHeap* cpuSrvHeap, DxGpuDescriptorHeap* gpuSrvHeap,
                DxGpuDescriptorHeap* cpuSamplerHeap, DxGpuDescriptorHeap* gpuSamplerHeap) {
        device_    = device;
        d3dDevice_ = d3dDevice;
        type_      = toCommandListType(queueType);

        HRESULT hr = d3dDevice->CreateCommandAllocator(type_, IID_PPV_ARGS(&allocator_));
        if (FAILED(hr)) return ErrorCode::Unknown;

        // Create descriptor staging (shared by all encoders from this pool).
        srvStaging_.init(cpuSrvHeap, gpuSrvHeap, d3dDevice,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024);
        samplerStaging_.init(cpuSamplerHeap, gpuSamplerHeap, d3dDevice,
                             D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64);

        return ErrorCode::Ok;
    }

    // ---- CommandPool interface ----
    Status createEncoder(CommandEncoder*& out) override;
    void   destroyEncoder(CommandEncoder*& encoder) override;

    void reset() override {
        releaseCommandBuffers();
        // Reset descriptor staging -- GPU is done (fence waited), so staging
        // bump pointers can safely return to start.
        srvStaging_.reset();
        samplerStaging_.reset();
        allocator_->Reset();
    }

    void cleanup() {
        releaseCommandBuffers();
        srvStaging_.destroy();
        samplerStaging_.destroy();
        allocator_.Reset();
    }

    // ---- Internal ----
    [[nodiscard]] ID3D12CommandAllocator* handle()          const { return allocator_.Get(); }
    [[nodiscard]] DxDeviceImpl*           ownerDevice()     const { return device_; }
    [[nodiscard]] DxDescriptorStaging*    srvStaging()            { return &srvStaging_; }
    [[nodiscard]] DxDescriptorStaging*    samplerStaging()        { return &samplerStaging_; }

    /// Called by DxCommandEncoderImpl::finish() to register a command buffer with this pool.
    void trackCommandBuffer(DxCommandBufferImpl* cb) { trackedBuffers_.push_back(cb); }

private:
    void releaseCommandBuffers() {
        for (auto* cb : trackedBuffers_) {
            cb->release();
            delete cb;
        }
        trackedBuffers_.clear();
    }

    ComPtr<ID3D12CommandAllocator>      allocator_;
    ID3D12Device*                       d3dDevice_ = nullptr;
    DxDeviceImpl*                       device_     = nullptr;
    D3D12_COMMAND_LIST_TYPE             type_       = D3D12_COMMAND_LIST_TYPE_DIRECT;
    std::vector<DxCommandBufferImpl*>   trackedBuffers_;
    DxDescriptorStaging                 srvStaging_;
    DxDescriptorStaging                 samplerStaging_;
};

} // namespace raptor::rhi::dx12
