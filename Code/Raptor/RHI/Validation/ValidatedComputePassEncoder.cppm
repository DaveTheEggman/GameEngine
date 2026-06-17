/// Validation wrapper for ComputePassEncoder.
/// Ported from Sedulous.RHI.Validation/ValidatedComputePassEncoder.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_compute_pass_encoder;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedCommandEncoder; // forward

class ValidatedComputePassEncoder : public ComputePassEncoder {
public:
    void begin(ComputePassEncoder* inner, ValidatedCommandEncoder* parent) {
        inner_ = inner; parent_ = parent;
        pipelineBound_ = false; ended_ = false;
    }

    void setPipeline(ComputePipeline* pipeline) override {
        if (ended_) { logError("[Validation] compute setPipeline: pass ended"); return; }
        if (!pipeline) { logError("[Validation] compute setPipeline: pipeline is null"); return; }
        pipelineBound_ = true;
        inner_->setPipeline(pipeline);
    }

    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        if (ended_) return;
        if (!group) { logError("[Validation] compute setBindGroup: group is null"); return; }
        if (!pipelineBound_) logWarning("[Validation] compute setBindGroup: no pipeline bound");
        inner_->setBindGroup(index, group, dynOffsets);
    }

    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (ended_) return;
        if (!pipelineBound_) logWarning("[Validation] compute setPushConstants: no pipeline bound");
        if (!data && size > 0) { logError("[Validation] compute setPushConstants: data is null"); return; }
        if (offset % 4 != 0) logError("[Validation] compute setPushConstants: offset not 4-byte aligned");
        if (size % 4 != 0) logError("[Validation] compute setPushConstants: size not 4-byte aligned");
        inner_->setPushConstants(stages, offset, size, data);
    }

    void dispatch(u32 x, u32 y, u32 z) override {
        if (ended_) { logError("[Validation] dispatch: pass ended"); return; }
        if (!pipelineBound_) { logError("[Validation] dispatch: no pipeline bound"); return; }
        if (x == 0 || y == 0 || z == 0) logWarning("[Validation] dispatch: zero dimension");
        inner_->dispatch(x, y, z);
    }

    void dispatchIndirect(Buffer* buffer, u64 offset) override {
        if (ended_) { logError("[Validation] dispatchIndirect: pass ended"); return; }
        if (!pipelineBound_) { logError("[Validation] dispatchIndirect: no pipeline bound"); return; }
        if (!buffer) { logError("[Validation] dispatchIndirect: buffer is null"); return; }
        inner_->dispatchIndirect(buffer, offset);
    }

    void computeBarrier() override {
        if (ended_) return;
        inner_->computeBarrier();
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        if (ended_) return;
        if (!qs) { logError("[Validation] compute writeTimestamp: querySet is null"); return; }
        inner_->writeTimestamp(qs, index);
    }

    void end() override;

private:
    ComputePassEncoder* inner_  = nullptr;
    ValidatedCommandEncoder* parent_ = nullptr;
    bool pipelineBound_ = false;
    bool ended_         = false;
};

} // namespace raptor::rhi::validation
