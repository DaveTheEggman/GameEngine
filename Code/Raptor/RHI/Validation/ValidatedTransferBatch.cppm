/// Validation wrapper for TransferBatch.
/// Ported from Sedulous.RHI.Validation/ValidatedTransferBatch.bf.

export module raptor.rhi.validation:validated_transfer_batch;

import raptor.core;
import raptor.rhi;
import :validated_fence;

export namespace raptor::rhi::validation {

class ValidatedTransferBatch : public TransferBatch {
public:
    explicit ValidatedTransferBatch(TransferBatch* inner) : inner_(inner) {}

    void writeBuffer(Buffer* dst, u64 dstOffset, Span<const u8> data) override {
        if (destroyed_) { logError("[Validation] TransferBatch::writeBuffer: batch already destroyed"); return; }
        if (!dst) { logError("[Validation] TransferBatch::writeBuffer: dst is null"); return; }
        if (data.count() == 0) { logWarning("[Validation] TransferBatch::writeBuffer: data is empty"); return; }
        pendingWrites_++;
        inner_->writeBuffer(dst, dstOffset, data);
    }

    void writeTexture(Texture* dst, Span<const u8> data, const TextureDataLayout& layout,
                      Extent3D extent, u32 mipLevel, u32 arrayLayer) override {
        if (destroyed_) { logError("[Validation] TransferBatch::writeTexture: batch already destroyed"); return; }
        if (!dst) { logError("[Validation] TransferBatch::writeTexture: dst is null"); return; }
        if (data.count() == 0) { logWarning("[Validation] TransferBatch::writeTexture: data is empty"); return; }
        if (extent.width == 0 || extent.height == 0) { logError("[Validation] TransferBatch::writeTexture: extent is zero"); return; }
        pendingWrites_++;
        inner_->writeTexture(dst, data, layout, extent, mipLevel, arrayLayer);
    }

    Status submit() override {
        if (destroyed_) { logError("[Validation] TransferBatch::submit: batch already destroyed"); return ErrorCode::Unknown; }
        if (pendingWrites_ == 0) logWarning("[Validation] TransferBatch::submit: no pending writes");
        pendingWrites_ = 0;
        return inner_->submit();
    }

    Status submitAsync(Fence* fence, u64 signalValue) override {
        if (destroyed_) { logError("[Validation] TransferBatch::submitAsync: batch already destroyed"); return ErrorCode::Unknown; }
        if (!fence) { logError("[Validation] TransferBatch::submitAsync: fence is null"); return ErrorCode::Unknown; }
        if (pendingWrites_ == 0) logWarning("[Validation] TransferBatch::submitAsync: no pending writes");
        pendingWrites_ = 0;
        auto* vf = dynamic_cast<ValidatedFence*>(fence);
        Fence* innerFence = vf ? vf->inner() : fence;
        if (vf) vf->trackSignal(signalValue);
        return inner_->submitAsync(innerFence, signalValue);
    }

    void reset() override { pendingWrites_ = 0; inner_->reset(); }

    void destroy() override {
        if (destroyed_) { logWarning("[Validation] TransferBatch::destroy: already destroyed"); return; }
        destroyed_ = true;
        inner_->destroy();
    }

    TransferBatch* inner() const { return inner_; }

private:
    TransferBatch* inner_;
    bool destroyed_    = false;
    i32  pendingWrites_ = 0;
};

} // namespace raptor::rhi::validation
