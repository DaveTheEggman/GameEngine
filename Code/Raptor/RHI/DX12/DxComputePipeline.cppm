/// DX12 implementation of ComputePipeline.
/// Ported from Sedulous.RHI.DX12/DX12ComputePipeline.bf.

module;

#include "DxIncludes.h"

export module raptor.rhi.dx12:compute_pipeline;

import raptor.core;
import raptor.rhi;
import :pipeline_layout;
import :shader_module;

export namespace raptor::rhi::dx12 {

class DxComputePipelineImpl : public ComputePipeline {
public:
    Status init(ID3D12Device* device, const ComputePipelineDesc& d) {
        layout_ = static_cast<DxPipelineLayoutImpl*>(d.layout);
        if (!layout_) return ErrorCode::Unknown;
        auto* csMod = static_cast<DxShaderModuleImpl*>(d.compute.module);
        if (!csMod) return ErrorCode::Unknown;

        auto cs = csMod->bytecode();
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = layout_->handle();
        pso.CS = { cs.data(), cs.count() };

        HRESULT hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipelineState_));
        if (FAILED(hr)) {
            logErrorf("DxComputePipeline: CreateComputePipelineState failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }
        return ErrorCode::Ok;
    }

    void cleanup() { pipelineState_.Reset(); }

    [[nodiscard]] ID3D12PipelineState*  handle()         const { return pipelineState_.Get(); }
    [[nodiscard]] DxPipelineLayoutImpl* pipelineLayout() const { return layout_; }

private:
    ComPtr<ID3D12PipelineState> pipelineState_;
    DxPipelineLayoutImpl*       layout_ = nullptr;
};

} // namespace raptor::rhi::dx12
