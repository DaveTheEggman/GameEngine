/// DX12 implementation of RenderPassEncoder + MeshShaderPassExt.
/// Records render pass commands into the parent command encoder's command list.
/// Ported from Sedulous.RHI.DX12/DX12RenderPassEncoder.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:render_pass_encoder;

import raptor.core;
import raptor.rhi;
import :conversions;
import :buffer;
import :bind_group;
import :bind_group_layout;
import :render_pipeline;
import :pipeline_layout;
import :query_set;
import :texture;
import :texture_view;
import :descriptor_staging;
import :gpu_descriptor_heap;
import :mesh_pipeline;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

/// Pointers needed by the render pass encoder, provided by the command encoder.
/// Avoids coupling to DxCommandEncoderImpl directly.
struct DxRenderPassContext {
    ID3D12GraphicsCommandList* cmdList  = nullptr;
    DxDescriptorStaging*       srvStaging     = nullptr;
    DxDescriptorStaging*       samplerStaging = nullptr;
    DxGpuDescriptorHeap*       gpuSrvHeap     = nullptr;
    DxGpuDescriptorHeap*       gpuSamplerHeap = nullptr;
    // Indirect command signatures (cached on device).
    ID3D12CommandSignature*    drawSig            = nullptr;
    ID3D12CommandSignature*    drawIndexedSig     = nullptr;
    ID3D12CommandSignature*    dispatchMeshSig    = nullptr;
};

class DxRenderPassEncoderImpl : public RenderPassEncoder, public MeshShaderPassExt {
public:
    MeshShaderPassExt* AsMeshShaderExt() noexcept override { return this; }
    explicit DxRenderPassEncoderImpl(const DxRenderPassContext& ctx)
        : ctx_(ctx) {}

    void begin(const RenderPassDesc& desc) {
        desc_ = desc;
        currentPipeline_     = nullptr;
        currentMeshPipeline_ = nullptr;
    }

    // ---- RenderPassEncoder: Pipeline & Binding ----

    void SetPipeline(RenderPipeline* pipeline) override {
        auto* dxPipeline = static_cast<DxRenderPipelineImpl*>(pipeline);
        if (!dxPipeline) return;
        currentPipeline_     = dxPipeline;
        currentMeshPipeline_ = nullptr;

        auto* cmdList = ctx_.cmdList;
        cmdList->SetPipelineState(dxPipeline->handle());
        cmdList->SetGraphicsRootSignature(dxPipeline->pipelineLayout()->handle());
        cmdList->IASetPrimitiveTopology(dxPipeline->topology());

        // Re-apply cached vertex buffers with correct strides from the new pipeline.
        // DX12 vertex buffer views include stride, which comes from the pipeline.
        // If setVertexBuffer was called before setPipeline, the stride was 0.
        for (u32 slot = 0; slot < cachedVbCount_; ++slot) {
            if (cachedVbs_[slot].BufferLocation != 0) {
                cachedVbs_[slot].StrideInBytes = dxPipeline->getVertexStride(slot);
                cmdList->IASetVertexBuffers(slot, 1, &cachedVbs_[slot]);
            }
        }
    }

    void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynamicOffsets) override {
        auto* dxGroup = static_cast<DxBindGroupImpl*>(group);
        if (!dxGroup) return;

        auto* layout = getCurrentLayout();
        if (!layout) return;

        auto* cmdList  = ctx_.cmdList;
        auto* dxLayout = static_cast<DxBindGroupLayoutImpl*>(dxGroup->Layout());

        // Copy-on-bind: copy bind group's descriptors into encoder's staging region,
        // then bind from the staging offset. This makes bind group destruction safe
        // during command recording -- the GPU only references the staging copy.

        // Bind CBV/SRV/UAV table (staged).
        if (dxGroup->cbvSrvUavOffset() >= 0 && dxLayout && dxLayout->cbvSrvUavCount() > 0) {
            i32 rootIdx = layout->getCbvSrvUavRootIndex(index);
            if (rootIdx >= 0) {
                i32 stagedOffset = ctx_.srvStaging->copyFrom(
                    static_cast<u32>(dxGroup->cbvSrvUavOffset()), dxLayout->cbvSrvUavCount());
                if (stagedOffset >= 0) {
                    auto gpuHandle = ctx_.gpuSrvHeap->getGpuHandle(static_cast<u32>(stagedOffset));
                    cmdList->SetGraphicsRootDescriptorTable(static_cast<UINT>(rootIdx), gpuHandle);
                }
            }
        }

        // Bind sampler table (staged).
        if (dxGroup->samplerOffset() >= 0 && dxLayout && dxLayout->samplerCount() > 0) {
            i32 rootIdx = layout->getSamplerRootIndex(index);
            if (rootIdx >= 0) {
                i32 stagedOffset = ctx_.samplerStaging->copyFrom(
                    static_cast<u32>(dxGroup->samplerOffset()), dxLayout->samplerCount());
                if (stagedOffset >= 0) {
                    auto gpuHandle = ctx_.gpuSamplerHeap->getGpuHandle(static_cast<u32>(stagedOffset));
                    cmdList->SetGraphicsRootDescriptorTable(static_cast<UINT>(rootIdx), gpuHandle);
                }
            }
        }

        // Bind dynamic offset root descriptors (not staged -- uses GPU virtual addresses).
        auto dynAddrs = dxGroup->dynamicGpuAddresses();
        usize dynOffsetIdx = 0;
        for (usize i = 0; i < layout->dynamicRootEntries().Size(); ++i) {
            const auto& entry = layout->dynamicRootEntries()[i];
            if (entry.groupIndex != index) continue;
            if (entry.dynamicIndex >= static_cast<u32>(dynAddrs.Size())) continue;

            u64 gpuAddr = dynAddrs[entry.dynamicIndex];
            if (dynOffsetIdx < dynamicOffsets.Size())
                gpuAddr += static_cast<u64>(dynamicOffsets[dynOffsetIdx]);
            ++dynOffsetIdx;

            switch (entry.paramType) {
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                cmdList->SetGraphicsRootConstantBufferView(static_cast<UINT>(entry.rootParamIndex), gpuAddr);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
                cmdList->SetGraphicsRootShaderResourceView(static_cast<UINT>(entry.rootParamIndex), gpuAddr);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                cmdList->SetGraphicsRootUnorderedAccessView(static_cast<UINT>(entry.rootParamIndex), gpuAddr);
                break;
            default: break;
            }
        }
    }

    void SetPushConstants(ShaderStage /*stages*/, u32 offset, u32 size, const void* data) override {
        auto* layout = getCurrentLayout();
        if (!layout || layout->pushConstantRootIndex() < 0) return;

        ctx_.cmdList->SetGraphicsRoot32BitConstants(
            static_cast<UINT>(layout->pushConstantRootIndex()),
            size / 4, data, offset / 4);
    }

    // ---- Vertex & Index Buffers ----

    void SetVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override {
        auto* dxBuf = static_cast<DxBufferImpl*>(buffer);
        if (!dxBuf || slot >= 8) return;

        u32 stride = currentPipeline_ ? currentPipeline_->getVertexStride(slot) : 0;

        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = dxBuf->gpuAddress() + offset;
        view.SizeInBytes    = static_cast<UINT>(dxBuf->desc.size - offset);
        view.StrideInBytes  = stride;

        // Cache for re-application when pipeline changes.
        cachedVbs_[slot] = view;
        if (slot >= cachedVbCount_) cachedVbCount_ = slot + 1;

        ctx_.cmdList->IASetVertexBuffers(slot, 1, &view);
    }

    void SetIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override {
        auto* dxBuf = static_cast<DxBufferImpl*>(buffer);
        if (!dxBuf) return;

        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = dxBuf->gpuAddress() + offset;
        view.SizeInBytes    = static_cast<UINT>(dxBuf->desc.size - offset);
        view.Format         = toDxgiIndexFormat(format);

        ctx_.cmdList->IASetIndexBuffer(&view);
    }

    // ---- Dynamic State ----

    void SetViewport(f32 x, f32 y, f32 w, f32 h, f32 minDepth, f32 maxDepth) override {
        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = x;
        viewport.TopLeftY = y;
        viewport.Width    = w;
        viewport.Height   = h;
        viewport.MinDepth = minDepth;
        viewport.MaxDepth = maxDepth;

        ctx_.cmdList->RSSetViewports(1, &viewport);
    }

    void SetScissor(i32 x, i32 y, u32 w, u32 h) override {
        D3D12_RECT rect{};
        rect.left   = x;
        rect.top    = y;
        rect.right  = x + static_cast<LONG>(w);
        rect.bottom = y + static_cast<LONG>(h);

        ctx_.cmdList->RSSetScissorRects(1, &rect);
    }

    void SetBlendConstant(f32 r, f32 g, f32 b, f32 a) override {
        f32 color[4] = { r, g, b, a };
        ctx_.cmdList->OMSetBlendFactor(color);
    }

    void SetStencilReference(u32 reference) override {
        ctx_.cmdList->OMSetStencilRef(reference);
    }

    // ---- Draw Commands ----

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        ctx_.cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex, u32 firstInstance) override {
        ctx_.cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void DrawIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        auto* dxBuf = static_cast<DxBufferImpl*>(buffer);
        if (!dxBuf) return;

        auto* sig = ctx_.drawSig;
        if (!sig) return;

        u32 actualStride = (stride > 0) ? stride : 16; // sizeof(D3D12_DRAW_ARGUMENTS)
        for (u32 i = 0; i < drawCount; ++i) {
            ctx_.cmdList->ExecuteIndirect(sig, 1, dxBuf->handle(),
                offset + static_cast<u64>(i) * actualStride, nullptr, 0);
        }
    }

    void DrawIndexedIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        auto* dxBuf = static_cast<DxBufferImpl*>(buffer);
        if (!dxBuf) return;

        auto* sig = ctx_.drawIndexedSig;
        if (!sig) return;

        u32 actualStride = (stride > 0) ? stride : 20; // sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
        for (u32 i = 0; i < drawCount; ++i) {
            ctx_.cmdList->ExecuteIndirect(sig, 1, dxBuf->handle(),
                offset + static_cast<u64>(i) * actualStride, nullptr, 0);
        }
    }

    // ---- Queries ----

    void WriteTimestamp(QuerySet* querySet, u32 index) override {
        auto* qs = static_cast<DxQuerySetImpl*>(querySet);
        if (qs) ctx_.cmdList->EndQuery(qs->handle(), D3D12_QUERY_TYPE_TIMESTAMP, index);
    }

    void BeginOcclusionQuery(QuerySet* querySet, u32 index) override {
        auto* qs = static_cast<DxQuerySetImpl*>(querySet);
        if (qs) ctx_.cmdList->BeginQuery(qs->handle(), D3D12_QUERY_TYPE_OCCLUSION, index);
    }

    void EndOcclusionQuery(QuerySet* querySet, u32 index) override {
        auto* qs = static_cast<DxQuerySetImpl*>(querySet);
        if (qs) ctx_.cmdList->EndQuery(qs->handle(), D3D12_QUERY_TYPE_OCCLUSION, index);
    }

    // ---- MeshShaderPassExt ----

    void SetMeshPipeline(MeshPipeline* pipeline) override {
        auto* dxPipeline = static_cast<DxMeshPipelineImpl*>(pipeline);
        if (!dxPipeline) return;
        currentMeshPipeline_ = dxPipeline;
        currentPipeline_     = nullptr; // clear regular pipeline

        auto* cmdList = ctx_.cmdList;
        cmdList->SetPipelineState(dxPipeline->handle());
        cmdList->SetGraphicsRootSignature(dxPipeline->pipelineLayout()->handle());
    }

    void DrawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override {
        // Need ID3D12GraphicsCommandList6 for DispatchMesh.
        ID3D12GraphicsCommandList6* cmdList6 = nullptr;
        HRESULT hr = ctx_.cmdList->QueryInterface(IID_PPV_ARGS(&cmdList6));
        if (SUCCEEDED(hr) && cmdList6) {
            cmdList6->DispatchMesh(groupCountX, groupCountY, groupCountZ);
            cmdList6->Release();
        }
    }

    void DrawMeshTasksIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        auto* dxBuf = static_cast<DxBufferImpl*>(buffer);
        if (!dxBuf) return;

        auto* sig = ctx_.dispatchMeshSig;
        if (!sig) return;

        u32 actualStride = (stride > 0) ? stride : 12; // sizeof(D3D12_DISPATCH_MESH_ARGUMENTS): 3 x u32
        for (u32 i = 0; i < drawCount; ++i) {
            ctx_.cmdList->ExecuteIndirect(sig, 1, dxBuf->handle(),
                offset + static_cast<u64>(i) * actualStride, nullptr, 0);
        }
    }

    void DrawMeshTasksIndirectCount(Buffer* buffer, u64 offset,
                                    Buffer* countBuffer, u64 countOffset,
                                    u32 maxDrawCount, u32 /*stride*/) override {
        auto* dxBuf      = static_cast<DxBufferImpl*>(buffer);
        auto* dxCountBuf = static_cast<DxBufferImpl*>(countBuffer);
        if (!dxBuf || !dxCountBuf) return;

        auto* sig = ctx_.dispatchMeshSig;
        if (!sig) return;

        ctx_.cmdList->ExecuteIndirect(sig, maxDrawCount, dxBuf->handle(),
            offset, dxCountBuf->handle(), countOffset);
    }

    // ---- End ----

    void End() override {
        // Timestamp at pass end.
        if (desc_.timestampQuerySet) {
            auto* qs = static_cast<DxQuerySetImpl*>(desc_.timestampQuerySet);
            if (qs)
                ctx_.cmdList->EndQuery(qs->handle(), D3D12_QUERY_TYPE_TIMESTAMP, desc_.endTimestampIndex);
        }

        // MSAA resolve: resolve multisampled color attachments to their resolve targets.
        auto colorAtts = desc_.colorAttachments.View();
        for (usize i = 0; i < colorAtts.Size(); ++i) {
            const auto& ca = colorAtts[i];
            if (!ca.resolveTarget) continue;

            auto* srcView = static_cast<DxTextureViewImpl*>(ca.view);
            auto* dstView = static_cast<DxTextureViewImpl*>(ca.resolveTarget);
            if (!srcView || !dstView) continue;

            auto* srcTex = srcView->dxTexture();
            auto* dstTex = dstView->dxTexture();

            TextureFormat format = srcView->Format();
            if (format == TextureFormat::Undefined)
                format = srcView->dxTexture()->desc.format;
            DXGI_FORMAT dxgiFormat = toDxgiFormat(format);

            // Transition src to resolve source, dst to resolve dest.
            D3D12_RESOURCE_BARRIER barriers[2]{};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource   = srcTex->handle();
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource   = dstTex->handle();
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_RESOLVE_DEST;
            barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            ctx_.cmdList->ResourceBarrier(2, barriers);

            ctx_.cmdList->ResolveSubresource(dstTex->handle(), 0, srcTex->handle(), 0, dxgiFormat);

            // Transition back.
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
            barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

            ctx_.cmdList->ResourceBarrier(2, barriers);
        }

        currentPipeline_     = nullptr;
        currentMeshPipeline_ = nullptr;
    }

private:
    DxPipelineLayoutImpl* getCurrentLayout() {
        if (currentPipeline_)
            return currentPipeline_->pipelineLayout();
        if (currentMeshPipeline_)
            return currentMeshPipeline_->pipelineLayout();
        return nullptr;
    }

    DxRenderPassContext    ctx_;
    RenderPassDesc         desc_{};
    DxRenderPipelineImpl*  currentPipeline_     = nullptr;
    DxMeshPipelineImpl*    currentMeshPipeline_ = nullptr;

    // Cached vertex buffer views for re-application on pipeline change.
    D3D12_VERTEX_BUFFER_VIEW cachedVbs_[8]{};
    u32                      cachedVbCount_ = 0;
};

} // namespace raptor::rhi::dx12
