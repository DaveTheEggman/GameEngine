/// Validation wrapper for CommandEncoder + RayTracingEncoderExt.
/// Ported from Sedulous.RHI.Validation/ValidatedCommandEncoder.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_command_encoder;

import raptor.core;
import raptor.rhi;
import :validated_render_pass_encoder;
import :validated_compute_pass_encoder;

using namespace raptor::core;

export namespace raptor::rhi::validation {

enum class EncoderState { Recording, InRenderPass, InComputePass, Finished };

class ValidatedCommandEncoder : public CommandEncoder, public RayTracingEncoderExt {
public:
    RayTracingEncoderExt* asRayTracingExt() noexcept override { return this; }
    explicit ValidatedCommandEncoder(CommandEncoder* inner)
        : inner_(inner) {}

    // Called by sub-encoders when their end() fires.
    void onPassEnded() { state_ = EncoderState::Recording; }

    // ---- CommandEncoder ----

    RenderPassEncoder* beginRenderPass(const RenderPassDesc& desc) override {
        if (!checkState("beginRenderPass", EncoderState::Recording)) return &rpe_;
        if (desc.colorAttachments.IsEmpty() &&
            (!desc.depthStencilAttachment.HasValue() || !desc.depthStencilAttachment->view))
            logWarning("[Validation] beginRenderPass: no color or depth attachment");
        for (usize i = 0; i < desc.colorAttachments.count; ++i)
            if (!desc.colorAttachments[i].view)
                logErrorf("[Validation] beginRenderPass: color attachment %d view is null", static_cast<int>(i));

        state_ = EncoderState::InRenderPass;
        auto* innerRpe = inner_->beginRenderPass(desc);
        rpe_.begin(innerRpe, this);
        return &rpe_;
    }

    ComputePassEncoder* beginComputePass(StringView label) override {
        if (!checkState("beginComputePass", EncoderState::Recording)) return &cpe_;
        state_ = EncoderState::InComputePass;
        auto* innerCpe = inner_->beginComputePass(label);
        cpe_.begin(innerCpe, this);
        return &cpe_;
    }

    void barrier(const BarrierGroup& group) override {
        if (!checkState("barrier", EncoderState::Recording)) return;
        inner_->barrier(group);
    }

    void copyBufferToBuffer(Buffer* src, u64 srcOff, Buffer* dst, u64 dstOff, u64 size) override {
        if (!checkState("copyBufferToBuffer", EncoderState::Recording)) return;
        if (!src) { logError("[Validation] copyBufferToBuffer: src is null"); return; }
        if (!dst) { logError("[Validation] copyBufferToBuffer: dst is null"); return; }
        if (size == 0) logWarning("[Validation] copyBufferToBuffer: size is 0");
        inner_->copyBufferToBuffer(src, srcOff, dst, dstOff, size);
    }

    void copyBufferToTexture(Buffer* src, Texture* dst, const BufferTextureCopyRegion& r) override {
        if (!checkState("copyBufferToTexture", EncoderState::Recording)) return;
        if (!src) { logError("[Validation] copyBufferToTexture: src is null"); return; }
        if (!dst) { logError("[Validation] copyBufferToTexture: dst is null"); return; }
        inner_->copyBufferToTexture(src, dst, r);
    }

    void copyTextureToBuffer(Texture* src, Buffer* dst, const BufferTextureCopyRegion& r) override {
        if (!checkState("copyTextureToBuffer", EncoderState::Recording)) return;
        if (!src) { logError("[Validation] copyTextureToBuffer: src is null"); return; }
        if (!dst) { logError("[Validation] copyTextureToBuffer: dst is null"); return; }
        inner_->copyTextureToBuffer(src, dst, r);
    }

    void copyTextureToTexture(Texture* src, Texture* dst, const TextureCopyRegion& r) override {
        if (!checkState("copyTextureToTexture", EncoderState::Recording)) return;
        if (!src) { logError("[Validation] copyTextureToTexture: src is null"); return; }
        if (!dst) { logError("[Validation] copyTextureToTexture: dst is null"); return; }
        inner_->copyTextureToTexture(src, dst, r);
    }

    void blit(Texture* src, Texture* dst) override {
        if (!checkState("blit", EncoderState::Recording)) return;
        if (!src || !dst) { logError("[Validation] blit: src or dst is null"); return; }
        inner_->blit(src, dst);
    }

    void generateMipmaps(Texture* tex) override {
        if (!checkState("generateMipmaps", EncoderState::Recording)) return;
        if (!tex) { logError("[Validation] generateMipmaps: texture is null"); return; }
        inner_->generateMipmaps(tex);
    }

    void resolveTexture(Texture* src, Texture* dst) override {
        if (!checkState("resolveTexture", EncoderState::Recording)) return;
        if (!src || !dst) { logError("[Validation] resolveTexture: src or dst is null"); return; }
        inner_->resolveTexture(src, dst);
    }

    void resetQuerySet(QuerySet* qs, u32 first, u32 count) override {
        if (!checkState("resetQuerySet", EncoderState::Recording)) return;
        if (!qs) { logError("[Validation] resetQuerySet: querySet is null"); return; }
        inner_->resetQuerySet(qs, first, count);
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        if (!checkState("writeTimestamp", EncoderState::Recording)) return;
        if (!qs) { logError("[Validation] writeTimestamp: querySet is null"); return; }
        inner_->writeTimestamp(qs, index);
    }

    void resolveQuerySet(QuerySet* qs, u32 first, u32 count, Buffer* dst, u64 dstOff) override {
        if (!checkState("resolveQuerySet", EncoderState::Recording)) return;
        if (!qs) { logError("[Validation] resolveQuerySet: querySet is null"); return; }
        if (!dst) { logError("[Validation] resolveQuerySet: dst is null"); return; }
        inner_->resolveQuerySet(qs, first, count, dst, dstOff);
    }

    void beginDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        if (state_ == EncoderState::Finished) { logError("[Validation] beginDebugLabel: encoder finished"); return; }
        debugLabelDepth_++;
        inner_->beginDebugLabel(label, r, g, b, a);
    }

    void endDebugLabel() override {
        if (state_ == EncoderState::Finished) { logError("[Validation] endDebugLabel: encoder finished"); return; }
        if (debugLabelDepth_ <= 0) { logError("[Validation] endDebugLabel: no matching begin"); return; }
        debugLabelDepth_--;
        inner_->endDebugLabel();
    }

    void insertDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        if (state_ == EncoderState::Finished) return;
        inner_->insertDebugLabel(label, r, g, b, a);
    }

    CommandBuffer* finish() override {
        if (state_ == EncoderState::Finished) { logError("[Validation] finish: encoder already finished"); return nullptr; }
        if (state_ == EncoderState::InRenderPass) logError("[Validation] finish: render pass still open");
        if (state_ == EncoderState::InComputePass) logError("[Validation] finish: compute pass still open");
        if (debugLabelDepth_ > 0) logWarningf("[Validation] finish: %d debug label(s) not closed", debugLabelDepth_);
        state_ = EncoderState::Finished;
        return inner_->finish();
    }

    // ---- RayTracingEncoderExt ----

    void buildBottomLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOff,
                                     Span<const AccelStructGeometryTriangles> tris,
                                     Span<const AccelStructGeometryAABBs> aabbs) override {
        if (!checkState("buildBLAS", EncoderState::Recording)) return;
        if (!dst) { logError("[Validation] buildBLAS: dst is null"); return; }
        if (!scratch) { logError("[Validation] buildBLAS: scratch is null"); return; }
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->buildBottomLevelAccelStruct(dst, scratch, scratchOff, tris, aabbs);
        else logError("[Validation] buildBLAS: inner encoder does not support ray tracing");
    }

    void buildTopLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOff,
                                  Buffer* instanceBuf, u64 instanceOff, u32 instanceCount) override {
        if (!checkState("buildTLAS", EncoderState::Recording)) return;
        if (!dst || !scratch || !instanceBuf) { logError("[Validation] buildTLAS: null argument"); return; }
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->buildTopLevelAccelStruct(dst, scratch, scratchOff, instanceBuf, instanceOff, instanceCount);
        else logError("[Validation] buildTLAS: inner encoder does not support ray tracing");
    }

    void setRayTracingPipeline(RayTracingPipeline* pipeline) override {
        if (!checkState("setRayTracingPipeline", EncoderState::Recording)) return;
        if (!pipeline) { logError("[Validation] setRayTracingPipeline: pipeline is null"); return; }
        rtPipelineBound_ = true;
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->setRayTracingPipeline(pipeline);
    }

    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        if (!checkState("RT setBindGroup", EncoderState::Recording)) return;
        if (!group) { logError("[Validation] RT setBindGroup: group is null"); return; }
        if (!rtPipelineBound_) logWarning("[Validation] RT setBindGroup: no RT pipeline bound");
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->setBindGroup(index, group, dynOffsets);
    }

    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (!checkState("RT setPushConstants", EncoderState::Recording)) return;
        if (!rtPipelineBound_) logWarning("[Validation] RT setPushConstants: no RT pipeline bound");
        if (!data && size > 0) { logError("[Validation] RT setPushConstants: data is null"); return; }
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->setPushConstants(stages, offset, size, data);
    }

    void traceRays(Buffer* raygenSBT, u64 raygenOff, u64 raygenStride,
                   Buffer* missSBT, u64 missOff, u64 missStride,
                   Buffer* hitSBT, u64 hitOff, u64 hitStride,
                   u32 width, u32 height, u32 depth) override {
        if (!checkState("traceRays", EncoderState::Recording)) return;
        if (!rtPipelineBound_) { logError("[Validation] traceRays: no RT pipeline bound"); return; }
        if (!raygenSBT) { logError("[Validation] traceRays: raygenSBT is null"); return; }
        auto* rt = inner_->asRayTracingExt();
        if (rt) rt->traceRays(raygenSBT, raygenOff, raygenStride, missSBT, missOff, missStride,
                              hitSBT, hitOff, hitStride, width, height, depth);
    }

    CommandEncoder* inner() const { return inner_; }

private:
    bool checkState(const char* method, EncoderState expected) {
        if (state_ == EncoderState::Finished) {
            logErrorf("[Validation] %s: encoder already finished", method);
            return false;
        }
        if (state_ != expected) {
            logErrorf("[Validation] %s: wrong state (expected Recording, got %s)", method,
                      state_ == EncoderState::InRenderPass ? "InRenderPass" :
                      state_ == EncoderState::InComputePass ? "InComputePass" : "?");
            return false;
        }
        return true;
    }

    CommandEncoder* inner_;
    EncoderState    state_ = EncoderState::Recording;
    i32             debugLabelDepth_ = 0;
    bool            rtPipelineBound_ = false;

    ValidatedRenderPassEncoder  rpe_;
    ValidatedComputePassEncoder cpe_;
};

// ---- Deferred end() implementations ----

void ValidatedRenderPassEncoder::end() {
    if (ended_) { logError("[Validation] RenderPassEncoder::end: already ended"); return; }
    ended_ = true;
    inner_->end();
    if (parent_) parent_->onPassEnded();
}

void ValidatedComputePassEncoder::end() {
    if (ended_) { logError("[Validation] ComputePassEncoder::end: already ended"); return; }
    ended_ = true;
    inner_->end();
    if (parent_) parent_->onPassEnded();
}

} // namespace raptor::rhi::validation
