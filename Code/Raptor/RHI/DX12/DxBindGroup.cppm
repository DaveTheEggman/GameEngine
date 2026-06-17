/// DX12 implementation of BindGroup.
/// Allocates contiguous descriptor ranges in CPU-visible heaps and writes descriptors.
/// Ported from Sedulous.RHI.DX12/DX12BindGroup.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:bind_group;

import raptor.core;
import raptor.rhi;
import :conversions;
import :bind_group_layout;
import :gpu_descriptor_heap;
import :buffer;
import :texture;
import :texture_view;
import :sampler;
import :accel_struct;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxAccelStructImpl; // forward

class DxBindGroupImpl : public BindGroup {
public:
    Status init(ID3D12Device* device, const BindGroupDesc& d,
                DxGpuDescriptorHeap* cpuSrvHeap, DxGpuDescriptorHeap* cpuSamplerHeap) {
        device_         = device;
        layout_         = static_cast<DxBindGroupLayoutImpl*>(d.layout);
        cpuSrvHeap_     = cpuSrvHeap;
        cpuSamplerHeap_ = cpuSamplerHeap;
        if (!layout_) return ErrorCode::Unknown;

        // Cache counts so cleanup doesn't need to access layout_ (which may be destroyed first).
        cachedCbvSrvUavCount_ = layout_->cbvSrvUavCount();
        cachedSamplerCount_   = layout_->samplerCount();

        if (cachedCbvSrvUavCount_ > 0) {
            cbvSrvUavOffset_ = cpuSrvHeap->allocate(cachedCbvSrvUavCount_);
            if (cbvSrvUavOffset_ < 0) return ErrorCode::Unknown;
        }
        if (cachedSamplerCount_ > 0) {
            samplerOffset_ = cpuSamplerHeap->allocate(cachedSamplerCount_);
            if (samplerOffset_ < 0) return ErrorCode::Unknown;
        }

        writeDescriptors(d);
        return ErrorCode::Ok;
    }

    BindGroupLayout* Layout() override { return layout_; }

    void UpdateBindless(Span<const BindlessUpdateEntry> entries) override {
        auto ranges = layout_->ranges();
        for (usize i = 0; i < entries.Size(); ++i) {
            const auto& e = entries[i];
            if (e.layoutIndex >= static_cast<u32>(ranges.Size())) continue;
            const auto& r = ranges[e.layoutIndex];

            BindGroupEntry bgEntry{};
            bgEntry.buffer      = e.buffer;
            bgEntry.bufferOffset= e.bufferOffset;
            bgEntry.bufferSize  = e.bufferSize;
            bgEntry.textureView = e.textureView;
            bgEntry.sampler     = e.sampler;

            if (r.isSampler)
                writeSampler(bgEntry, r, e.arrayIndex);
            else
                writeCbvSrvUav(bgEntry, r, e.arrayIndex);
        }
    }

    void cleanup() {
        if (cbvSrvUavOffset_ >= 0 && cachedCbvSrvUavCount_ > 0)
            cpuSrvHeap_->free(static_cast<u32>(cbvSrvUavOffset_), cachedCbvSrvUavCount_);
        if (samplerOffset_ >= 0 && cachedSamplerCount_ > 0)
            cpuSamplerHeap_->free(static_cast<u32>(samplerOffset_), cachedSamplerCount_);
        cbvSrvUavOffset_ = -1; samplerOffset_ = -1;
    }

    [[nodiscard]] i32 cbvSrvUavOffset()  const { return cbvSrvUavOffset_; }
    [[nodiscard]] i32 samplerOffset()    const { return samplerOffset_; }
    [[nodiscard]] Span<const u64> dynamicGpuAddresses() const { return { dynAddrs_.Data(), dynAddrs_.Size() }; }

private:
    void writeDescriptors(const BindGroupDesc& d) {
        auto ranges = layout_->ranges();
        usize entryIdx = 0;
        for (usize i = 0; i < ranges.Size(); ++i) {
            const auto& r = ranges[i];
            switch (r.type) {
            case BindingType::BindlessTextures: case BindingType::BindlessSamplers:
            case BindingType::BindlessStorageBuffers: case BindingType::BindlessStorageTextures:
                continue;
            default: break;
            }
            if (entryIdx >= d.entries.Size()) break;
            const auto& e = d.entries[entryIdx++];

            if (r.hasDynamicOffset) {
                if (auto* buf = static_cast<DxBufferImpl*>(e.buffer))
                    dynAddrs_.PushBack(buf->gpuAddress() + e.bufferOffset);
                else
                    dynAddrs_.PushBack(0);
                continue;
            }
            if (r.isSampler) writeSampler(e, r);
            else             writeCbvSrvUav(e, r);
        }
    }

    void writeCbvSrvUav(const BindGroupEntry& e, const DxBindingRangeInfo& r, u32 arrayIdx = 0) {
        u32 off = static_cast<u32>(cbvSrvUavOffset_) + r.heapOffset + arrayIdx;
        D3D12_CPU_DESCRIPTOR_HANDLE dest = cpuSrvHeap_->getCpuHandle(off);

        switch (r.type) {
        case BindingType::UniformBuffer:
            if (auto* buf = static_cast<DxBufferImpl*>(e.buffer)) {
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                cbv.BufferLocation = buf->gpuAddress() + e.bufferOffset;
                u64 sz = (e.bufferSize > 0) ? e.bufferSize : buf->desc.size;
                cbv.SizeInBytes = static_cast<UINT>((sz + 255) & ~u64(255));
                device_->CreateConstantBufferView(&cbv, dest);
            }
            break;
        case BindingType::StorageBufferReadOnly:
            if (auto* buf = static_cast<DxBufferImpl*>(e.buffer)) {
                u64 sz = (e.bufferSize > 0) ? e.bufferSize : buf->desc.size;
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                if (r.storageBufferStride > 0) {
                    srv.Format = DXGI_FORMAT_UNKNOWN;
                    srv.Buffer.FirstElement = static_cast<UINT64>(e.bufferOffset / r.storageBufferStride);
                    srv.Buffer.NumElements  = static_cast<UINT>(sz / r.storageBufferStride);
                    srv.Buffer.StructureByteStride = r.storageBufferStride;
                } else {
                    srv.Format = DXGI_FORMAT_R32_TYPELESS;
                    srv.Buffer.FirstElement = static_cast<UINT64>(e.bufferOffset / 4);
                    srv.Buffer.NumElements  = static_cast<UINT>(sz / 4);
                    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                }
                device_->CreateShaderResourceView(buf->handle(), &srv, dest);
            }
            break;
        case BindingType::StorageBufferReadWrite:
            if (auto* buf = static_cast<DxBufferImpl*>(e.buffer)) {
                u64 sz = (e.bufferSize > 0) ? e.bufferSize : buf->desc.size;
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                if (r.storageBufferStride > 0) {
                    uav.Format = DXGI_FORMAT_UNKNOWN;
                    uav.Buffer.FirstElement = static_cast<UINT64>(e.bufferOffset / r.storageBufferStride);
                    uav.Buffer.NumElements  = static_cast<UINT>(sz / r.storageBufferStride);
                    uav.Buffer.StructureByteStride = r.storageBufferStride;
                } else {
                    uav.Format = DXGI_FORMAT_R32_TYPELESS;
                    uav.Buffer.FirstElement = static_cast<UINT64>(e.bufferOffset / 4);
                    uav.Buffer.NumElements  = static_cast<UINT>(sz / 4);
                    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                }
                device_->CreateUnorderedAccessView(buf->handle(), nullptr, &uav, dest);
            }
            break;
        case BindingType::SampledTexture: case BindingType::BindlessTextures:
            if (auto* v = static_cast<DxTextureViewImpl*>(e.textureView))
                device_->CopyDescriptorsSimple(1, dest, v->getSrv(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            break;
        case BindingType::StorageTextureReadOnly: case BindingType::StorageTextureReadWrite:
        case BindingType::BindlessStorageTextures:
            if (auto* v = static_cast<DxTextureViewImpl*>(e.textureView))
                device_->CopyDescriptorsSimple(1, dest, v->getUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            break;
        case BindingType::AccelerationStructure:
            if (auto* dxAs = static_cast<DxAccelStructImpl*>(e.accelStruct)) {
                D3D12_SHADER_RESOURCE_VIEW_DESC asSrv{};
                asSrv.Format = DXGI_FORMAT_UNKNOWN;
                asSrv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                asSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                asSrv.RaytracingAccelerationStructure.Location = dxAs->DeviceAddress();
                device_->CreateShaderResourceView(nullptr, &asSrv, dest);
            }
            break;
        default: break;
        }
    }

    void writeSampler(const BindGroupEntry& e, const DxBindingRangeInfo& r, u32 arrayIdx = 0) {
        u32 off = static_cast<u32>(samplerOffset_) + r.heapOffset + arrayIdx;
        D3D12_CPU_DESCRIPTOR_HANDLE dest = cpuSamplerHeap_->getCpuHandle(off);
        if (auto* s = static_cast<DxSamplerImpl*>(e.sampler))
            device_->CopyDescriptorsSimple(1, dest, s->handle(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    ID3D12Device*              device_         = nullptr;
    DxBindGroupLayoutImpl*     layout_         = nullptr;
    DxGpuDescriptorHeap*       cpuSrvHeap_     = nullptr;
    DxGpuDescriptorHeap*       cpuSamplerHeap_ = nullptr;
    u32                        cachedCbvSrvUavCount_ = 0;
    u32                        cachedSamplerCount_   = 0;
    i32                        cbvSrvUavOffset_ = -1;
    i32                        samplerOffset_   = -1;
    Array<u64>                 dynAddrs_;
};

} // namespace raptor::rhi::dx12
