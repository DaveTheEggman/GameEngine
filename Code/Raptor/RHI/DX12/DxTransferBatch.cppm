/// DX12 implementation of TransferBatch.
/// Ported from Sedulous.RHI.DX12/DX12TransferBatch.bf.

module;

#include "DxIncludes.h"

#include <cstring>
#include <vector>

export module raptor.rhi.dx12:transfer_batch;

import raptor.core;
import raptor.rhi;
import :conversions;
import :buffer;
import :texture;
import :fence;

export namespace raptor::rhi::dx12 {

class DxQueueImpl; // forward

class DxTransferBatchImpl : public TransferBatch {
public:
    Status init(ID3D12Device* device, ID3D12CommandQueue* queue, QueueType queueType) {
        device_ = device;
        queue_  = queue;

        HRESULT hr = device->CreateCommandAllocator(
            toCommandListType(queueType),
            IID_PPV_ARGS(&allocator_));
        if (FAILED(hr)) {
            logErrorf("DxTransferBatch: CreateCommandAllocator failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        hr = device->CreateCommandList(0,
            toCommandListType(queueType),
            allocator_.Get(), nullptr,
            IID_PPV_ARGS(&cmdList_));
        if (FAILED(hr)) {
            logErrorf("DxTransferBatch: CreateCommandList failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        // Command list starts open; close it until we need it.
        cmdList_->Close();

        // Create fence for synchronous submit.
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        if (FAILED(hr)) {
            logErrorf("DxTransferBatch: CreateFence failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fenceValue_ = 0;

        return ErrorCode::Ok;
    }

    // ---- TransferBatch interface ----

    void writeBuffer(Buffer* dst, u64 dstOffset, Span<const u8> data) override {
        auto* dxDst = static_cast<DxBufferImpl*>(dst);
        if (!dxDst || data.count() == 0) return;

        ensureRecording();

        // Create upload-heap staging buffer.
        u64 stagingSize = static_cast<u64>(data.count());
        ComPtr<ID3D12Resource> staging;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = stagingSize;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&staging));
        if (FAILED(hr)) return;

        // Map and copy data.
        void* mapped = nullptr;
        staging->Map(0, nullptr, &mapped);
        std::memcpy(mapped, data.data(), data.count());
        staging->Unmap(0, nullptr);

        stagingBuffers_.push_back(std::move(staging));

        // Record copy command.
        cmdList_->CopyBufferRegion(dxDst->handle(), dstOffset,
            stagingBuffers_.back().Get(), 0, stagingSize);
    }

    void writeTexture(Texture* dst, Span<const u8> data,
                      const TextureDataLayout& layout, Extent3D extent,
                      u32 mipLevel, u32 arrayLayer) override {
        auto* dxTex = static_cast<DxTextureImpl*>(dst);
        if (!dxTex || data.count() == 0) return;

        ensureRecording();

        // Calculate aligned row pitch (D3D12 requires 256-byte row alignment).
        u32 alignedRowPitch = (layout.bytesPerRow + 255) & ~u32(255);
        u32 rowsPerImage    = (layout.rowsPerImage > 0) ? layout.rowsPerImage : extent.height;
        u64 stagingSize     = static_cast<u64>(alignedRowPitch) * rowsPerImage * extent.depth;

        ComPtr<ID3D12Resource> staging;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = stagingSize;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&staging));
        if (FAILED(hr)) return;

        // Map and copy data row by row (handles pitch alignment).
        void* mapped = nullptr;
        staging->Map(0, nullptr, &mapped);

        const u8* srcPtr = data.data() + layout.offset;
        u8*       dstPtr = static_cast<u8*>(mapped);
        for (u32 z = 0; z < extent.depth; ++z) {
            for (u32 row = 0; row < rowsPerImage; ++row) {
                std::memcpy(
                    dstPtr + (z * rowsPerImage + row) * alignedRowPitch,
                    srcPtr + (z * rowsPerImage + row) * layout.bytesPerRow,
                    layout.bytesPerRow);
            }
        }

        staging->Unmap(0, nullptr);
        stagingBuffers_.push_back(std::move(staging));

        // Transition texture to copy dest.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = dxTex->handle();
        barrier.Transition.StateBefore = dxTex->currentState();
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (dxTex->currentState() != D3D12_RESOURCE_STATE_COPY_DEST)
            cmdList_->ResourceBarrier(1, &barrier);

        u32 subresource = mipLevel + arrayLayer * dxTex->desc.mipLevelCount;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource                          = stagingBuffers_.back().Get();
        srcLoc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset             = 0;
        srcLoc.PlacedFootprint.Footprint.Format   = toDxgiFormat(dxTex->desc.format);
        srcLoc.PlacedFootprint.Footprint.Width    = extent.width;
        srcLoc.PlacedFootprint.Footprint.Height   = extent.height;
        srcLoc.PlacedFootprint.Footprint.Depth    = extent.depth;
        srcLoc.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = dxTex->handle();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = subresource;

        cmdList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        // Transition back to common.
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
        cmdList_->ResourceBarrier(1, &barrier);
        dxTex->setState(D3D12_RESOURCE_STATE_COMMON);
    }

    Status submit() override {
        if (!isRecording_) return ErrorCode::Ok;

        cmdList_->Close();
        isRecording_ = false;

        ID3D12CommandList* lists[] = { cmdList_.Get() };
        queue_->ExecuteCommandLists(1, lists);

        // Wait for completion.
        ++fenceValue_;
        queue_->Signal(fence_.Get(), fenceValue_);
        if (fence_->GetCompletedValue() < fenceValue_) {
            fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
            WaitForSingleObject(fenceEvent_, INFINITE);
        }

        releaseStagingBuffers();
        return ErrorCode::Ok;
    }

    Status submitAsync(Fence* fence, u64 signalValue) override {
        if (!isRecording_) return ErrorCode::Ok;

        cmdList_->Close();
        isRecording_ = false;

        ID3D12CommandList* lists[] = { cmdList_.Get() };
        queue_->ExecuteCommandLists(1, lists);

        if (auto* dxFence = static_cast<DxFenceImpl*>(fence))
            queue_->Signal(dxFence->handle(), signalValue);

        // Note: staging buffers can't be released until GPU is done.
        // Caller must wait on the fence before calling reset().
        return ErrorCode::Ok;
    }

    void reset() override {
        releaseStagingBuffers();
    }

    void destroy() override {
        releaseStagingBuffers();

        if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
        fence_.Reset();
        cmdList_.Reset();
        allocator_.Reset();
    }

private:
    void ensureRecording() {
        if (!isRecording_) {
            allocator_->Reset();
            cmdList_->Reset(allocator_.Get(), nullptr);
            isRecording_ = true;
        }
    }

    void releaseStagingBuffers() {
        stagingBuffers_.clear();
    }

    ID3D12Device*                       device_  = nullptr;
    ID3D12CommandQueue*                 queue_   = nullptr;
    ComPtr<ID3D12CommandAllocator>      allocator_;
    ComPtr<ID3D12GraphicsCommandList>   cmdList_;
    bool                                isRecording_ = false;

    std::vector<ComPtr<ID3D12Resource>> stagingBuffers_;

    ComPtr<ID3D12Fence>                 fence_;
    u64                                 fenceValue_ = 0;
    HANDLE                              fenceEvent_ = nullptr;
};

} // namespace raptor::rhi::dx12
