/// DX12 implementation of Queue.
/// Ported from Sedulous.RHI.DX12/DX12Queue.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:queue;

import raptor.core;
import raptor.rhi;
import :conversions;
import :command_buffer;
import :fence;
import :transfer_batch;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl; // forward

class DxQueueImpl : public Queue {
public:
    Status init(ID3D12Device* device, QueueType type, DxDeviceImpl* owner) {
        queueType  = type;
        device_    = owner;
        d3dDevice_ = device;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = toCommandListType(type);
        HRESULT hr = device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_));
        if (FAILED(hr)) return ErrorCode::Unknown;

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&internalFence_));
        if (FAILED(hr)) return ErrorCode::Unknown;
        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        // Query timestamp frequency.
        UINT64 freq = 0;
        queue_->GetTimestampFrequency(&freq);
        tsPeriod_ = (freq > 0) ? (1e9f / static_cast<f32>(freq)) : 1.0f;

        return ErrorCode::Ok;
    }

    // ---- Queue interface ----

    void Submit(Span<CommandBuffer* const> cmdBufs) override {
        if (cmdBufs.Size() == 0) return;
        Array<ID3D12CommandList*> lists(cmdBufs.Size());
        for (usize i = 0; i < cmdBufs.Size(); ++i) {
            if (auto* dxCb = static_cast<DxCommandBufferImpl*>(cmdBufs[i]))
                lists[i] = dxCb->handle();
        }
        queue_->ExecuteCommandLists(static_cast<UINT>(lists.Size()), lists.Data());
    }

    void Submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) override {
        Submit(cmdBufs);
        if (auto* f = static_cast<DxFenceImpl*>(signalFence))
            queue_->Signal(f->handle(), signalValue);
    }

    void Submit(Span<CommandBuffer* const> cmdBufs,
                Span<Fence* const> waitFences, Span<const u64> waitValues,
                Fence* signalFence, u64 signalValue) override {
        for (usize i = 0; i < waitFences.Size(); ++i)
            if (auto* f = static_cast<DxFenceImpl*>(waitFences[i]))
                queue_->Wait(f->handle(), waitValues[i]);
        Submit(cmdBufs);
        if (auto* f = static_cast<DxFenceImpl*>(signalFence))
            queue_->Signal(f->handle(), signalValue);
    }

    void WaitIdle() override {
        ++fenceValue_;
        queue_->Signal(internalFence_.Get(), fenceValue_);
        if (internalFence_->GetCompletedValue() < fenceValue_) {
            internalFence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
            WaitForSingleObject(fenceEvent_, INFINITE);
        }
    }

    Status CreateTransferBatch(TransferBatch*& out) override {
        auto* batch = new DxTransferBatchImpl();
        if (batch->init(d3dDevice_, queue_.Get(), queueType) != ErrorCode::Ok) {
            delete batch;
            return ErrorCode::Unknown;
        }
        out = batch;
        return ErrorCode::Ok;
    }

    void DestroyTransferBatch(TransferBatch*& batch) override {
        if (auto* dx = static_cast<DxTransferBatchImpl*>(batch)) {
            dx->Destroy();
            delete dx;
        }
        batch = nullptr;
    }

    f32 TimestampPeriod() const override { return tsPeriod_; }

    void cleanup() {
        if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
        internalFence_.Reset();
        queue_.Reset();
    }

    // ---- Internal ----
    [[nodiscard]] ID3D12CommandQueue* handle() const { return queue_.Get(); }
    [[nodiscard]] DxDeviceImpl*       owner()  const { return device_; }

private:
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence>        internalFence_;
    HANDLE                     fenceEvent_ = nullptr;
    u64                        fenceValue_ = 0;
    f32                        tsPeriod_   = 1.0f;
    DxDeviceImpl*              device_     = nullptr;
    ID3D12Device*              d3dDevice_  = nullptr;
};

} // namespace raptor::rhi::dx12
