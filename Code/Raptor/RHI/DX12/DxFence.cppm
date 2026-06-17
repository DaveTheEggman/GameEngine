/// DX12 implementation of Fence using ID3D12Fence.
/// Ported from Sedulous.RHI.DX12/DX12Fence.bf.

module;

#include "DxIncludes.h"

export module raptor.rhi.dx12:fence;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::dx12 {

class DxFenceImpl : public Fence {
public:
    Status init(ID3D12Device* device, u64 initialValue) {
        HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        if (FAILED(hr)) {
            logErrorf("DxFence: CreateFence failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return ErrorCode::Ok;
    }

    u64 completedValue() override {
        return fence_->GetCompletedValue();
    }

    bool wait(u64 value, u64 timeoutNs) override {
        if (fence_->GetCompletedValue() >= value) return true;
        fence_->SetEventOnCompletion(value, event_);
        DWORD timeoutMs = (timeoutNs == ~0ull) ? INFINITE : static_cast<DWORD>(timeoutNs / 1000000);
        return WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0;
    }

    void cleanup() {
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        fence_.Reset();
    }

    // ---- Internal ----
    [[nodiscard]] ID3D12Fence* handle() const { return fence_.Get(); }

    void signal(ID3D12CommandQueue* queue, u64 value) {
        queue->Signal(fence_.Get(), value);
    }

private:
    ComPtr<ID3D12Fence> fence_;
    HANDLE              event_ = nullptr;
};

} // namespace raptor::rhi::dx12
