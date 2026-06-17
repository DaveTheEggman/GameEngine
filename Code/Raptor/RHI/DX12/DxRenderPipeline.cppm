/// DX12 implementation of RenderPipeline.
/// Wraps a D3D12 graphics pipeline state object.
/// Ported from Sedulous.RHI.DX12/DX12RenderPipeline.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <algorithm>
#include <cstring>

export module raptor.rhi.dx12:render_pipeline;

import raptor.core;
import raptor.rhi;
import :conversions;
import :pipeline_layout;
import :shader_module;
import :pipeline_cache;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxRenderPipelineImpl : public RenderPipeline {
public:
    Status init(ID3D12Device* device, const RenderPipelineDesc& d) {
        layout_ = static_cast<DxPipelineLayoutImpl*>(d.layout);
        if (!layout_) return ErrorCode::Unknown;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = layout_->handle();

        // Vertex shader.
        auto* vsMod = static_cast<DxShaderModuleImpl*>(d.vertex.shader.module);
        if (!vsMod) return ErrorCode::Unknown;
        auto vsCode = vsMod->bytecode();
        pso.VS = { vsCode.Data(), vsCode.Size() };

        // Fragment shader.
        if (d.fragment.HasValue()) {
            auto* psMod = static_cast<DxShaderModuleImpl*>(d.fragment->shader.module);
            if (psMod) { auto ps = psMod->bytecode(); pso.PS = { ps.Data(), ps.Size() }; }
        }

        // Input layout.
        Array<D3D12_INPUT_ELEMENT_DESC> elems;
        auto bufs = d.vertex.buffers;
        vtxBufCount_ = static_cast<u32>(std::min(bufs.Size(), usize(8)));
        for (usize i = 0; i < bufs.Size(); ++i) {
            const auto& buf = bufs[i];
            if (i < 8) vtxStrides_[i] = buf.stride;
            auto attrs = buf.attributes;
            for (usize j = 0; j < attrs.Size(); ++j) {
                const auto& a = attrs[j];
                D3D12_INPUT_ELEMENT_DESC e{};
                e.SemanticName  = "TEXCOORD";
                e.SemanticIndex = a.shaderLocation;
                e.Format        = toDxgiVertexFormat(a.format);
                e.InputSlot     = static_cast<UINT>(i);
                e.AlignedByteOffset = a.offset;
                e.InputSlotClass = (buf.stepMode == VertexStepMode::Instance)
                    ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                e.InstanceDataStepRate = (buf.stepMode == VertexStepMode::Instance) ? 1 : 0;
                elems.PushBack(e);
            }
        }
        pso.InputLayout = { elems.Data(), static_cast<UINT>(elems.Size()) };

        // Topology.
        pso.PrimitiveTopologyType = toPrimitiveTopologyType(d.primitive.topology);
        topology_ = toPrimitiveTopology(d.primitive.topology);

        // Rasterizer.
        pso.RasterizerState.FillMode = toFillMode(d.primitive.fillMode);
        pso.RasterizerState.CullMode = toCullMode(d.primitive.cullMode);
        pso.RasterizerState.FrontCounterClockwise = (d.primitive.frontFace == FrontFace::CCW) ? TRUE : FALSE;
        pso.RasterizerState.DepthClipEnable = d.primitive.depthClipEnabled ? TRUE : FALSE;
        pso.RasterizerState.MultisampleEnable = (d.multisample.count > 1) ? TRUE : FALSE;

        if (d.depthStencil.HasValue()) {
            pso.RasterizerState.DepthBias = d.depthStencil->depthBias;
            pso.RasterizerState.DepthBiasClamp = d.depthStencil->depthBiasClamp;
            pso.RasterizerState.SlopeScaledDepthBias = d.depthStencil->depthBiasSlopeScale;
        }

        // Blend.
        Span<const ColorTargetState> targets = d.fragment.HasValue() ? d.fragment->targets : Span<const ColorTargetState>();
        pso.BlendState.AlphaToCoverageEnable = d.multisample.alphaToCoverageEnabled ? TRUE : FALSE;
        pso.BlendState.IndependentBlendEnable = (targets.Size() > 1) ? TRUE : FALSE;
        for (usize i = 0; i < targets.Size() && i < 8; ++i) {
            const auto& t = targets[i];
            auto& rt = pso.BlendState.RenderTarget[i];
            rt.RenderTargetWriteMask = static_cast<UINT8>(t.writeMask);
            if (t.blend.HasValue()) {
                rt.BlendEnable = TRUE;
                rt.SrcBlend      = toBlendFactor(t.blend->color.srcFactor);
                rt.DestBlend     = toBlendFactor(t.blend->color.dstFactor);
                rt.BlendOp       = toBlendOp(t.blend->color.operation);
                rt.SrcBlendAlpha = toBlendFactor(t.blend->alpha.srcFactor);
                rt.DestBlendAlpha= toBlendFactor(t.blend->alpha.dstFactor);
                rt.BlendOpAlpha  = toBlendOp(t.blend->alpha.operation);
            }
        }

        // Depth/stencil.
        if (d.depthStencil.HasValue()) {
            const auto& ds = *d.depthStencil;
            pso.DepthStencilState.DepthEnable    = ds.depthTestEnabled ? TRUE : FALSE;
            pso.DepthStencilState.DepthWriteMask  = ds.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            pso.DepthStencilState.DepthFunc       = toComparisonFunc(ds.depthCompare);
            pso.DepthStencilState.StencilEnable   = ds.stencilEnabled ? TRUE : FALSE;
            pso.DepthStencilState.StencilReadMask = ds.stencilReadMask;
            pso.DepthStencilState.StencilWriteMask= ds.stencilWriteMask;

            auto& ff = pso.DepthStencilState.FrontFace;
            ff.StencilFailOp      = toStencilOp(ds.stencilFront.failOp);
            ff.StencilDepthFailOp = toStencilOp(ds.stencilFront.depthFailOp);
            ff.StencilPassOp      = toStencilOp(ds.stencilFront.passOp);
            ff.StencilFunc        = toComparisonFunc(ds.stencilFront.compare);

            auto& bf = pso.DepthStencilState.BackFace;
            bf.StencilFailOp      = toStencilOp(ds.stencilBack.failOp);
            bf.StencilDepthFailOp = toStencilOp(ds.stencilBack.depthFailOp);
            bf.StencilPassOp      = toStencilOp(ds.stencilBack.passOp);
            bf.StencilFunc        = toComparisonFunc(ds.stencilBack.compare);

            pso.DSVFormat = toDxgiFormat(ds.format);
        }

        // Render targets.
        pso.NumRenderTargets = static_cast<UINT>(std::min(targets.Size(), usize(8)));
        for (usize i = 0; i < targets.Size() && i < 8; ++i)
            pso.RTVFormats[i] = toDxgiFormat(targets[i].format);

        // Multisample.
        pso.SampleDesc.Count = std::max(d.multisample.count, 1u);
        pso.SampleMask = (d.multisample.mask != 0) ? d.multisample.mask : ~0u;

        // Create PSO.
        HRESULT hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_));
        if (FAILED(hr)) {
            logErrorf("DxRenderPipeline: CreateGraphicsPipelineState failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }
        return ErrorCode::Ok;
    }

    void cleanup() { pipelineState_.Reset(); }

    [[nodiscard]] ID3D12PipelineState*    handle()   const { return pipelineState_.Get(); }
    [[nodiscard]] D3D_PRIMITIVE_TOPOLOGY  topology() const { return topology_; }
    [[nodiscard]] DxPipelineLayoutImpl*   pipelineLayout() const { return layout_; }
    [[nodiscard]] u32 getVertexStride(u32 slot) const { return (slot < vtxBufCount_) ? vtxStrides_[slot] : 0; }

private:
    ComPtr<ID3D12PipelineState>  pipelineState_;
    DxPipelineLayoutImpl*        layout_      = nullptr;
    D3D_PRIMITIVE_TOPOLOGY       topology_    = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    u32                          vtxStrides_[8]{};
    u32                          vtxBufCount_ = 0;
};

} // namespace raptor::rhi::dx12
