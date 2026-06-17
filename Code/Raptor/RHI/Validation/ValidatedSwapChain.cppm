/// Validation wrapper for SwapChain.
/// Ported from Sedulous.RHI.Validation/ValidatedSwapChain.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_swap_chain;

import raptor.core;
import raptor.rhi;
import :validated_queue;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedSwapChain : public SwapChain {
public:
    explicit ValidatedSwapChain(SwapChain* inner) : inner_(inner) {}

    TextureFormat format()            const override { return inner_->format(); }
    u32           width()             const override { return inner_->width(); }
    u32           height()            const override { return inner_->height(); }
    u32           bufferCount()       const override { return inner_->bufferCount(); }
    u32           currentImageIndex() const override { return inner_->currentImageIndex(); }
    Texture*      currentTexture()          override { return inner_->currentTexture(); }
    TextureView*  currentTextureView()      override { return inner_->currentTextureView(); }

    Status acquireNextImage() override {
        if (imageAcquired_) logWarning("[Validation] SwapChain::acquireNextImage: image already acquired");
        Status r = inner_->acquireNextImage();
        if (r == ErrorCode::Ok) imageAcquired_ = true;
        return r;
    }

    Status present(Queue* queue) override {
        if (!imageAcquired_) logWarning("[Validation] SwapChain::present: no image acquired");
        imageAcquired_ = false;
        // Unwrap validated queue so the inner swap chain gets the raw VK queue.
        auto* vq = static_cast<ValidatedQueue*>(queue);
        return inner_->present(vq ? vq->inner() : queue);
    }

    Status resize(u32 w, u32 h) override {
        if (imageAcquired_) logError("[Validation] SwapChain::resize: cannot resize while image is acquired");
        if (w == 0 || h == 0) logError("[Validation] SwapChain::resize: dimensions are zero");
        return inner_->resize(w, h);
    }

    SwapChain* inner() const { return inner_; }

private:
    SwapChain* inner_;
    bool       imageAcquired_ = false;
};

} // namespace raptor::rhi::validation
