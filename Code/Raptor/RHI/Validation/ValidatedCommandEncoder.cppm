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
    RayTracingEncoderExt* AsRayTracingExt() noexcept override { return this; }
    explicit ValidatedCommandEncoder(CommandEncoder* inner)
        : inner_(inner) {}

    // Called by sub-encoders when their end() fires.
    void onPassEnded() { state_ = EncoderState::Recording; }

    // ---- CommandEncoder ----

    RenderPassEncoder* BeginRenderPass(const RenderPassDesc& desc) override {
        if (!checkState("beginRenderPass", EncoderState::Recording)) return &rpe_;
        if (desc.colorAttachments.IsEmpty() &&
            (!desc.depthStencilAttachment.HasValue() || !desc.depthStencilAttachment->view))
            LogWarning("[Validation] beginRenderPass: no color or depth attachment");
        for (usize i = 0; i < desc.colorAttachments.count; ++i)
            if (!desc.colorAttachments[i].view)
                LogErrorf("[Validation] beginRenderPass: color attachment %d view is null", static_cast<int>(i));

        state_ = EncoderState::InRenderPass;
        auto* innerRpe = inner_->BeginRenderPass(desc);
        rpe_.begin(innerRpe, this);
        return &rpe_;
    }

    ComputePassEncoder* BeginComputePass(StringView label) override {
        if (!checkState("beginComputePass", EncoderState::Recording)) return &cpe_;
        state_ = EncoderState::InComputePass;
        auto* innerCpe = inner_->BeginComputePass(label);
        cpe_.begin(innerCpe, this);
        return &cpe_;
    }

    void Barrier(const BarrierGroup& group) override {
        if (!checkState("barrier", EncoderState::Recording)) return;
        inner_->Barrier(group);
    }

    void CopyBufferToBuffer(Buffer* src, u64 srcOff, Buffer* dst, u64 dstOff, u64 size) override {
        if (!checkState("copyBufferToBuffer", EncoderState::Recording)) return;
        if (!src) { LogError("[Validation] copyBufferToBuffer: src is null"); return; }
        if (!dst) { LogError("[Validation] copyBufferToBuffer: dst is null"); return; }
        if (size == 0) LogWarning("[Validation] copyBufferToBuffer: size is 0");
        inner_->CopyBufferToBuffer(src, srcOff, dst, dstOff, size);
    }

    void CopyBufferToTexture(Buffer* src, Texture* dst, const BufferTextureCopyRegion& r) override {
        if (!checkState("copyBufferToTexture", EncoderState::Recording)) return;
        if (!src) { LogError("[Validation] copyBufferToTexture: src is null"); return; }
        if (!dst) { LogError("[Validation] copyBufferToTexture: dst is null"); return; }
        inner_->CopyBufferToTexture(src, dst, r);
    }

    void CopyTextureToBuffer(Texture* src, Buffer* dst, const BufferTextureCopyRegion& r) override {
        if (!checkState("copyTextureToBuffer", EncoderState::Recording)) return;
        if (!src) { LogError("[Validation] copyTextureToBuffer: src is null"); return; }
        if (!dst) { LogError("[Validation] copyTextureToBuffer: dst is null"); return; }
        inner_->CopyTextureToBuffer(src, dst, r);
    }

    void CopyTextureToTexture(Texture* src, Texture* dst, const TextureCopyRegion& r) override {
        if (!checkState("copyTextureToTexture", EncoderState::Recording)) return;
        if (!src) { LogError("[Validation] copyTextureToTexture: src is null"); return; }
        if (!dst) { LogError("[Validation] copyTextureToTexture: dst is null"); return; }
        inner_->CopyTextureToTexture(src, dst, r);
    }

    void Blit(Texture* src, Texture* dst) override {
        if (!checkState("blit", EncoderState::Recording)) return;
        if (!src || !dst) { LogError("[Validation] blit: src or dst is null"); return; }
        inner_->Blit(src, dst);
    }

    void GenerateMipmaps(Texture* tex) override {
        if (!checkState("generateMipmaps", EncoderState::Recording)) return;
        if (!tex) { LogError("[Validation] generateMipmaps: texture is null"); return; }
        inner_->GenerateMipmaps(tex);
    }

    void ResolveTexture(Texture* src, Texture* dst) override {
        if (!checkState("resolveTexture", EncoderState::Recording)) return;
        if (!src || !dst) { LogError("[Validation] resolveTexture: src or dst is null"); return; }
        inner_->ResolveTexture(src, dst);
    }

    void ResetQuerySet(QuerySet* qs, u32 first, u32 count) override {
        if (!checkState("resetQuerySet", EncoderState::Recording)) return;
        if (!qs) { LogError("[Validation] resetQuerySet: querySet is null"); return; }
        inner_->ResetQuerySet(qs, first, count);
    }

    void WriteTimestamp(QuerySet* qs, u32 index) override {
        if (!checkState("writeTimestamp", EncoderState::Recording)) return;
        if (!qs) { LogError("[Validation] writeTimestamp: querySet is null"); return; }
        inner_->WriteTimestamp(qs, index);
    }

    void ResolveQuerySet(QuerySet* qs, u32 first, u32 count, Buffer* dst, u64 dstOff) override {
        if (!checkState("resolveQuerySet", EncoderState::Recording)) return;
        if (!qs) { LogError("[Validation] resolveQuerySet: querySet is null"); return; }
        if (!dst) { LogError("[Validation] resolveQuerySet: dst is null"); return; }
        inner_->ResolveQuerySet(qs, first, count, dst, dstOff);
    }

    void BeginDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        if (state_ == EncoderState::Finished) { LogError("[Validation] beginDebugLabel: encoder finished"); return; }
        debugLabelDepth_++;
        inner_->BeginDebugLabel(label, r, g, b, a);
    }

    void EndDebugLabel() override {
        if (state_ == EncoderState::Finished) { LogError("[Validation] endDebugLabel: encoder finished"); return; }
        if (debugLabelDepth_ <= 0) { LogError("[Validation] endDebugLabel: no matching begin"); return; }
        debugLabelDepth_--;
        inner_->EndDebugLabel();
    }

    void InsertDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        if (state_ == EncoderState::Finished) return;
        inner_->InsertDebugLabel(label, r, g, b, a);
    }

    CommandBuffer* Finish() override {
        if (state_ == EncoderState::Finished) { LogError("[Validation] finish: encoder already finished"); return nullptr; }
        if (state_ == EncoderState::InRenderPass) LogError("[Validation] finish: render pass still open");
        if (state_ == EncoderState::InComputePass) LogError("[Validation] finish: compute pass still open");
        if (debugLabelDepth_ > 0) LogWarningf("[Validation] finish: %d debug label(s) not closed", debugLabelDepth_);
        state_ = EncoderState::Finished;
        return inner_->Finish();
    }

    // ---- RayTracingEncoderExt ----

    void BuildBottomLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOff,
                                     Span<const AccelStructGeometryTriangles> tris,
                                     Span<const AccelStructGeometryAABBs> aabbs) override {
        if (!checkState("buildBLAS", EncoderState::Recording)) return;
        if (!dst) { LogError("[Validation] buildBLAS: dst is null"); return; }
        if (!scratch) { LogError("[Validation] buildBLAS: scratch is null"); return; }
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->BuildBottomLevelAccelStruct(dst, scratch, scratchOff, tris, aabbs);
        else LogError("[Validation] buildBLAS: inner encoder does not support ray tracing");
    }

    void BuildTopLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOff,
                                  Buffer* instanceBuf, u64 instanceOff, u32 instanceCount) override {
        if (!checkState("buildTLAS", EncoderState::Recording)) return;
        if (!dst || !scratch || !instanceBuf) { LogError("[Validation] buildTLAS: null argument"); return; }
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->BuildTopLevelAccelStruct(dst, scratch, scratchOff, instanceBuf, instanceOff, instanceCount);
        else LogError("[Validation] buildTLAS: inner encoder does not support ray tracing");
    }

    void SetRayTracingPipeline(RayTracingPipeline* pipeline) override {
        if (!checkState("setRayTracingPipeline", EncoderState::Recording)) return;
        if (!pipeline) { LogError("[Validation] setRayTracingPipeline: pipeline is null"); return; }
        rtPipelineBound_ = true;
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->SetRayTracingPipeline(pipeline);
    }

    void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        if (!checkState("RT setBindGroup", EncoderState::Recording)) return;
        if (!group) { LogError("[Validation] RT setBindGroup: group is null"); return; }
        if (!rtPipelineBound_) LogWarning("[Validation] RT setBindGroup: no RT pipeline bound");
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->SetBindGroup(index, group, dynOffsets);
    }

    void SetPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (!checkState("RT setPushConstants", EncoderState::Recording)) return;
        if (!rtPipelineBound_) LogWarning("[Validation] RT setPushConstants: no RT pipeline bound");
        if (!data && size > 0) { LogError("[Validation] RT setPushConstants: data is null"); return; }
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->SetPushConstants(stages, offset, size, data);
    }

    void TraceRays(Buffer* raygenSBT, u64 raygenOff, u64 raygenStride,
                   Buffer* missSBT, u64 missOff, u64 missStride,
                   Buffer* hitSBT, u64 hitOff, u64 hitStride,
                   u32 width, u32 height, u32 depth) override {
        if (!checkState("traceRays", EncoderState::Recording)) return;
        if (!rtPipelineBound_) { LogError("[Validation] traceRays: no RT pipeline bound"); return; }
        if (!raygenSBT) { LogError("[Validation] traceRays: raygenSBT is null"); return; }
        auto* rt = inner_->AsRayTracingExt();
        if (rt) rt->TraceRays(raygenSBT, raygenOff, raygenStride, missSBT, missOff, missStride,
                              hitSBT, hitOff, hitStride, width, height, depth);
    }

    CommandEncoder* inner() const { return inner_; }

private:
    bool checkState(const char* method, EncoderState expected) {
        if (state_ == EncoderState::Finished) {
            LogErrorf("[Validation] %s: encoder already finished", method);
            return false;
        }
        if (state_ != expected) {
            LogErrorf("[Validation] %s: wrong state (expected Recording, got %s)", method,
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

void ValidatedRenderPassEncoder::End() {
    if (ended_) { LogError("[Validation] RenderPassEncoder::End: already ended"); return; }
    ended_ = true;
    inner_->End();
    if (parent_) parent_->onPassEnded();
}

void ValidatedComputePassEncoder::End() {
    if (ended_) { LogError("[Validation] ComputePassEncoder::End: already ended"); return; }
    ended_ = true;
    inner_->End();
    if (parent_) parent_->onPassEnded();
}

} // namespace raptor::rhi::validation
