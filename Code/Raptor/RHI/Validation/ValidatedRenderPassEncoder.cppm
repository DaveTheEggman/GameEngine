/// Validation wrapper for RenderPassEncoder + MeshShaderPassExt.
/// Ported from Sedulous.RHI.Validation/ValidatedRenderPassEncoder.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_render_pass_encoder;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedCommandEncoder; // forward

class ValidatedRenderPassEncoder : public RenderPassEncoder, public MeshShaderPassExt {
public:
    MeshShaderPassExt* AsMeshShaderExt() noexcept override { return this; }
    void begin(RenderPassEncoder* inner, ValidatedCommandEncoder* parent) {
        inner_ = inner; parent_ = parent;
        pipelineBound_ = false; viewportSet_ = false; scissorSet_ = false; ended_ = false;
        meshPipelineBound_ = false;
    }

    // ---- RenderPassEncoder ----

    void SetPipeline(RenderPipeline* pipeline) override {
        if (ended_) { LogError("[Validation] setPipeline: render pass ended"); return; }
        if (!pipeline) { LogError("[Validation] setPipeline: pipeline is null"); return; }
        pipelineBound_ = true; meshPipelineBound_ = false;
        inner_->SetPipeline(pipeline);
    }

    void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        if (ended_) { LogError("[Validation] setBindGroup: render pass ended"); return; }
        if (!group) { LogError("[Validation] setBindGroup: group is null"); return; }
        inner_->SetBindGroup(index, group, dynOffsets);
    }

    void SetPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (ended_) { LogError("[Validation] setPushConstants: render pass ended"); return; }
        if (!pipelineBound_ && !meshPipelineBound_) LogWarning("[Validation] setPushConstants: no pipeline bound");
        if (!data && size > 0) { LogError("[Validation] setPushConstants: data is null but size > 0"); return; }
        if (size == 0) { LogWarning("[Validation] setPushConstants: size is 0"); return; }
        if (offset % 4 != 0) LogError("[Validation] setPushConstants: offset must be 4-byte aligned");
        if (size % 4 != 0) LogError("[Validation] setPushConstants: size must be 4-byte aligned");
        inner_->SetPushConstants(stages, offset, size, data);
    }

    void SetVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override {
        if (ended_) { LogError("[Validation] setVertexBuffer: render pass ended"); return; }
        if (!buffer) { LogError("[Validation] setVertexBuffer: buffer is null"); return; }
        inner_->SetVertexBuffer(slot, buffer, offset);
    }

    void SetIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override {
        if (ended_) { LogError("[Validation] setIndexBuffer: render pass ended"); return; }
        if (!buffer) { LogError("[Validation] setIndexBuffer: buffer is null"); return; }
        inner_->SetIndexBuffer(buffer, format, offset);
    }

    void SetViewport(f32 x, f32 y, f32 w, f32 h, f32 minD, f32 maxD) override {
        if (ended_) { LogError("[Validation] setViewport: render pass ended"); return; }
        viewportSet_ = true;
        inner_->SetViewport(x, y, w, h, minD, maxD);
    }

    void SetScissor(i32 x, i32 y, u32 w, u32 h) override {
        if (ended_) { LogError("[Validation] setScissor: render pass ended"); return; }
        scissorSet_ = true;
        inner_->SetScissor(x, y, w, h);
    }

    void SetBlendConstant(f32 r, f32 g, f32 b, f32 a) override {
        if (ended_) return;
        inner_->SetBlendConstant(r, g, b, a);
    }

    void SetStencilReference(u32 ref) override {
        if (ended_) return;
        inner_->SetStencilReference(ref);
    }

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        if (!checkDrawReady("draw")) return;
        if (vertexCount == 0) LogWarning("[Validation] draw: vertexCount is 0");
        inner_->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex, u32 firstInstance) override {
        if (!checkDrawReady("drawIndexed")) return;
        if (indexCount == 0) LogWarning("[Validation] drawIndexed: indexCount is 0");
        inner_->DrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void DrawIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawIndirect")) return;
        if (!buffer) { LogError("[Validation] drawIndirect: buffer is null"); return; }
        inner_->DrawIndirect(buffer, offset, drawCount, stride);
    }

    void DrawIndexedIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawIndexedIndirect")) return;
        if (!buffer) { LogError("[Validation] drawIndexedIndirect: buffer is null"); return; }
        inner_->DrawIndexedIndirect(buffer, offset, drawCount, stride);
    }

    void WriteTimestamp(QuerySet* qs, u32 index) override {
        if (ended_) return;
        if (!qs) { LogError("[Validation] writeTimestamp: querySet is null"); return; }
        inner_->WriteTimestamp(qs, index);
    }

    void BeginOcclusionQuery(QuerySet* qs, u32 index) override {
        if (ended_) return;
        if (!qs) { LogError("[Validation] beginOcclusionQuery: querySet is null"); return; }
        inner_->BeginOcclusionQuery(qs, index);
    }

    void EndOcclusionQuery(QuerySet* qs, u32 index) override {
        if (ended_) return;
        inner_->EndOcclusionQuery(qs, index);
    }

    void End() override;

    // ---- MeshShaderPassExt ----

    void SetMeshPipeline(MeshPipeline* pipeline) override {
        if (ended_) { LogError("[Validation] setMeshPipeline: render pass ended"); return; }
        if (!pipeline) { LogError("[Validation] setMeshPipeline: pipeline is null"); return; }
        meshPipelineBound_ = true; pipelineBound_ = false;
        auto* mp = inner_->AsMeshShaderExt();
        if (mp) mp->SetMeshPipeline(pipeline);
        else LogError("[Validation] setMeshPipeline: inner encoder does not support mesh shaders");
    }

    void DrawMeshTasks(u32 gx, u32 gy, u32 gz) override {
        if (!checkDrawReady("drawMeshTasks")) return;
        auto* mp = inner_->AsMeshShaderExt();
        if (mp) mp->DrawMeshTasks(gx, gy, gz);
    }

    void DrawMeshTasksIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawMeshTasksIndirect")) return;
        if (!buf) { LogError("[Validation] drawMeshTasksIndirect: buffer is null"); return; }
        auto* mp = inner_->AsMeshShaderExt();
        if (mp) mp->DrawMeshTasksIndirect(buf, offset, drawCount, stride);
    }

    void DrawMeshTasksIndirectCount(Buffer* buf, u64 offset, Buffer* countBuf, u64 countOffset, u32 maxDrawCount, u32 stride) override {
        if (!checkDrawReady("drawMeshTasksIndirectCount")) return;
        if (!buf || !countBuf) { LogError("[Validation] drawMeshTasksIndirectCount: buffer is null"); return; }
        auto* mp = inner_->AsMeshShaderExt();
        if (mp) mp->DrawMeshTasksIndirectCount(buf, offset, countBuf, countOffset, maxDrawCount, stride);
    }

private:
    bool checkDrawReady(const char* method) {
        if (ended_) { LogErrorf("[Validation] %s: render pass ended", method); return false; }
        if (!pipelineBound_ && !meshPipelineBound_) { LogErrorf("[Validation] %s: no pipeline bound", method); return false; }
        if (!viewportSet_) { LogErrorf("[Validation] %s: viewport not set", method); return false; }
        if (!scissorSet_) { LogErrorf("[Validation] %s: scissor not set", method); return false; }
        return true;
    }

    RenderPassEncoder* inner_   = nullptr;
    ValidatedCommandEncoder* parent_ = nullptr;
    bool pipelineBound_     = false;
    bool meshPipelineBound_ = false;
    bool viewportSet_       = false;
    bool scissorSet_        = false;
    bool ended_             = false;
};

} // namespace raptor::rhi::validation
