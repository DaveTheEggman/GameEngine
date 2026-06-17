/// DX12 implementation of CommandBuffer.
/// Simple wrapper for a closed ID3D12GraphicsCommandList.
/// Ported from Sedulous.RHI.DX12/DX12CommandBuffer.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:command_buffer;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxCommandBufferImpl : public CommandBuffer {
public:
    explicit DxCommandBufferImpl(ID3D12GraphicsCommandList* cmdList) : cmdList_(cmdList) {}

    [[nodiscard]] ID3D12GraphicsCommandList* handle() const { return cmdList_; }

    void release() {
        if (cmdList_) { cmdList_->Release(); cmdList_ = nullptr; }
    }

private:
    ID3D12GraphicsCommandList* cmdList_ = nullptr; // raw, released via release()
};

} // namespace raptor::rhi::dx12
