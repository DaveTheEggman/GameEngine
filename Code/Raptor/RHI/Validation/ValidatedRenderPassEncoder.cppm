/// Validation wrapper for RenderPassEncoder + MeshShaderPassExt.
/// Ported from Sedulous.RHI.Validation/ValidatedRenderPassEncoder.bf.

export module raptor.rhi.validation:validated_render_pass_encoder;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::validation {

class ValidatedCommandEncoder; // forward

class ValidatedRenderPassEncoder : public RenderPassEncoder, public MeshShaderPassExt {
public:
    void begin(RenderPassEncoder* inner, ValidatedCommandEncoder* parent) {
        inner_ = inner; parent_ = parent;
        pipelineBound_ = false; viewportSet_ = false; scissorSet_ = false; ended_ = false;
        meshPipelineBound_ = false;
    }

    // ---- RenderPassEncoder ----

    void setPipeline(RenderPipeline* pipeline) override {
        if (ended_) { logError("[Validation] setPipeline: render pass ended"); return; }
        if (!pipeline) { logError("[Validation] setPipeline: pipeline is null"); return; }
        pipelineBound_ = true; meshPipelineBound_ = false;
        inner_->setPipeline(pipeline);
    }

    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        if (ended_) { logError("[Validation] setBindGroup: render pass ended"); return; }
        if (!group) { logError("[Validation] setBindGroup: group is null"); return; }
        inner_->setBindGroup(index, group, dynOffsets);
    }

    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (ended_) { logError("[Validation] setPushConstants: render pass ended"); return; }
        if (!pipelineBound_ && !meshPipelineBound_) logWarning("[Validation] setPushConstants: no pipeline bound");
        if (!data && size > 0) { logError("[Validation] setPushConstants: data is null but size > 0"); return; }
        if (size == 0) { logWarning("[Validation] setPushConstants: size is 0"); return; }
        if (offset % 4 != 0) logError("[Validation] setPushConstants: offset must be 4-byte aligned");
        if (size % 4 != 0) logError("[Validation] setPushConstants: size must be 4-byte aligned");
        inner_->setPushConstants(stages, offset, size, data);
    }

    void setVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override {
        if (ended_) { logError("[Validation] setVertexBuffer: render pass ended"); return; }
        if (!buffer) { logError("[Validation] setVertexBuffer: buffer is null"); return; }
        inner_->setVertexBuffer(slot, buffer, offset);
    }

    void setIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override {
        if (ended_) { logError("[Validation] setIndexBuffer: render pass ended"); return; }
        if (!buffer) { logError("[Validation] setIndexBuffer: buffer is null"); return; }
        inner_->setIndexBuffer(buffer, format, offset);
    }

    void setViewport(f32 x, f32 y, f32 w, f32 h, f32 minD, f32 maxD) override {
        if (ended_) { logError("[Validation] setViewport: render pass ended"); return; }
        viewportSet_ = true;
        inner_->setViewport(x, y, w, h, minD, maxD);
    }

    void setScissor(i32 x, i32 y, u32 w, u32 h) override {
        if (ended_) { logError("[Validation] setScissor: render pass ended"); return; }
        scissorSet_ = true;
        inner_->setScissor(x, y, w, h);
    }

    void setBlendConstant(f32 r, f32 g, f32 b, f32 a) override {
        if (ended_) return;
        inner_->setBlendConstant(r, g, b, a);
    }

    void setStencilReference(u32 ref) override {
        if (ended_) return;
        inner_->setStencilReference(ref);
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        if (!checkDrawReady("draw")) return;
        if (vertexCount == 0) logWarning("[Validation] draw: vertexCount is 0");
        inner_->draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex, u32 firstInstance) override {
        if (!checkDrawReady("drawIndexed")) return;
        if (indexCount == 0) logWarning("[Validation] drawIndexed: indexCount is 0");
        inner_->drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void drawIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawIndirect")) return;
        if (!buffer) { logError("[Validation] drawIndirect: buffer is null"); return; }
        inner_->drawIndirect(buffer, offset, drawCount, stride);
    }

    void drawIndexedIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawIndexedIndirect")) return;
        if (!buffer) { logError("[Validation] drawIndexedIndirect: buffer is null"); return; }
        inner_->drawIndexedIndirect(buffer, offset, drawCount, stride);
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        if (ended_) return;
        if (!qs) { logError("[Validation] writeTimestamp: querySet is null"); return; }
        inner_->writeTimestamp(qs, index);
    }

    void beginOcclusionQuery(QuerySet* qs, u32 index) override {
        if (ended_) return;
        if (!qs) { logError("[Validation] beginOcclusionQuery: querySet is null"); return; }
        inner_->beginOcclusionQuery(qs, index);
    }

    void endOcclusionQuery(QuerySet* qs, u32 index) override {
        if (ended_) return;
        inner_->endOcclusionQuery(qs, index);
    }

    void end() override;

    // ---- MeshShaderPassExt ----

    void setMeshPipeline(MeshPipeline* pipeline) override {
        if (ended_) { logError("[Validation] setMeshPipeline: render pass ended"); return; }
        if (!pipeline) { logError("[Validation] setMeshPipeline: pipeline is null"); return; }
        meshPipelineBound_ = true; pipelineBound_ = false;
        auto* mp = dynamic_cast<MeshShaderPassExt*>(inner_);
        if (mp) mp->setMeshPipeline(pipeline);
        else logError("[Validation] setMeshPipeline: inner encoder does not support mesh shaders");
    }

    void drawMeshTasks(u32 gx, u32 gy, u32 gz) override {
        if (!checkDrawReady("drawMeshTasks")) return;
        auto* mp = dynamic_cast<MeshShaderPassExt*>(inner_);
        if (mp) mp->drawMeshTasks(gx, gy, gz);
    }

    void drawMeshTasksIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) override {
        if (!checkDrawReady("drawMeshTasksIndirect")) return;
        if (!buf) { logError("[Validation] drawMeshTasksIndirect: buffer is null"); return; }
        auto* mp = dynamic_cast<MeshShaderPassExt*>(inner_);
        if (mp) mp->drawMeshTasksIndirect(buf, offset, drawCount, stride);
    }

    void drawMeshTasksIndirectCount(Buffer* buf, u64 offset, Buffer* countBuf, u64 countOffset, u32 maxDrawCount, u32 stride) override {
        if (!checkDrawReady("drawMeshTasksIndirectCount")) return;
        if (!buf || !countBuf) { logError("[Validation] drawMeshTasksIndirectCount: buffer is null"); return; }
        auto* mp = dynamic_cast<MeshShaderPassExt*>(inner_);
        if (mp) mp->drawMeshTasksIndirectCount(buf, offset, countBuf, countOffset, maxDrawCount, stride);
    }

private:
    bool checkDrawReady(const char* method) {
        if (ended_) { logErrorf("[Validation] %s: render pass ended", method); return false; }
        if (!pipelineBound_ && !meshPipelineBound_) { logErrorf("[Validation] %s: no pipeline bound", method); return false; }
        if (!viewportSet_) { logErrorf("[Validation] %s: viewport not set", method); return false; }
        if (!scissorSet_) { logErrorf("[Validation] %s: scissor not set", method); return false; }
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
