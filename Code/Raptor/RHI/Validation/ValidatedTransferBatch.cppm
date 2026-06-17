/// Validation wrapper for TransferBatch.
/// Ported from Sedulous.RHI.Validation/ValidatedTransferBatch.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_transfer_batch;

import raptor.core;
import raptor.rhi;
import :validated_fence;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedTransferBatch : public TransferBatch {
public:
    explicit ValidatedTransferBatch(TransferBatch* inner) : inner_(inner) {}

    void WriteBuffer(Buffer* dst, u64 dstOffset, Span<const u8> data) override {
        if (destroyed_) { LogError("[Validation] TransferBatch::writeBuffer: batch already destroyed"); return; }
        if (!dst) { LogError("[Validation] TransferBatch::writeBuffer: dst is null"); return; }
        if (data.Size() == 0) { LogWarning("[Validation] TransferBatch::writeBuffer: data is empty"); return; }
        pendingWrites_++;
        inner_->WriteBuffer(dst, dstOffset, data);
    }

    void WriteTexture(Texture* dst, Span<const u8> data, const TextureDataLayout& layout,
                      Extent3D extent, u32 mipLevel, u32 arrayLayer) override {
        if (destroyed_) { LogError("[Validation] TransferBatch::writeTexture: batch already destroyed"); return; }
        if (!dst) { LogError("[Validation] TransferBatch::writeTexture: dst is null"); return; }
        if (data.Size() == 0) { LogWarning("[Validation] TransferBatch::writeTexture: data is empty"); return; }
        if (extent.width == 0 || extent.height == 0) { LogError("[Validation] TransferBatch::writeTexture: extent is zero"); return; }
        pendingWrites_++;
        inner_->WriteTexture(dst, data, layout, extent, mipLevel, arrayLayer);
    }

    Status Submit() override {
        if (destroyed_) { LogError("[Validation] TransferBatch::submit: batch already destroyed"); return ErrorCode::Unknown; }
        if (pendingWrites_ == 0) LogWarning("[Validation] TransferBatch::submit: no pending writes");
        pendingWrites_ = 0;
        return inner_->Submit();
    }

    Status SubmitAsync(Fence* fence, u64 signalValue) override {
        if (destroyed_) { LogError("[Validation] TransferBatch::submitAsync: batch already destroyed"); return ErrorCode::Unknown; }
        if (!fence) { LogError("[Validation] TransferBatch::submitAsync: fence is null"); return ErrorCode::Unknown; }
        if (pendingWrites_ == 0) LogWarning("[Validation] TransferBatch::submitAsync: no pending writes");
        pendingWrites_ = 0;
        auto* vf = static_cast<ValidatedFence*>(fence);
        Fence* innerFence = vf ? vf->inner() : fence;
        if (vf) vf->trackSignal(signalValue);
        return inner_->SubmitAsync(innerFence, signalValue);
    }

    void Reset() override { pendingWrites_ = 0; inner_->Reset(); }

    void Destroy() override {
        if (destroyed_) { LogWarning("[Validation] TransferBatch::destroy: already destroyed"); return; }
        destroyed_ = true;
        inner_->Destroy();
    }

    TransferBatch* inner() const { return inner_; }

private:
    TransferBatch* inner_;
    bool destroyed_    = false;
    i32  pendingWrites_ = 0;
};

} // namespace raptor::rhi::validation
