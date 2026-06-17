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

    TextureFormat Format()            const override { return inner_->Format(); }
    u32           Width()             const override { return inner_->Width(); }
    u32           Height()            const override { return inner_->Height(); }
    u32           BufferCount()       const override { return inner_->BufferCount(); }
    u32           CurrentImageIndex() const override { return inner_->CurrentImageIndex(); }
    Texture*      CurrentTexture()          override { return inner_->CurrentTexture(); }
    TextureView*  CurrentTextureView()      override { return inner_->CurrentTextureView(); }

    Status AcquireNextImage() override {
        if (imageAcquired_) LogWarning("[Validation] SwapChain::acquireNextImage: image already acquired");
        Status r = inner_->AcquireNextImage();
        if (r == ErrorCode::Ok) imageAcquired_ = true;
        return r;
    }

    Status Present(Queue* queue) override {
        if (!imageAcquired_) LogWarning("[Validation] SwapChain::present: no image acquired");
        imageAcquired_ = false;
        // Unwrap validated queue so the inner swap chain gets the raw VK queue.
        auto* vq = static_cast<ValidatedQueue*>(queue);
        return inner_->Present(vq ? vq->inner() : queue);
    }

    Status Resize(u32 w, u32 h) override {
        if (imageAcquired_) LogError("[Validation] SwapChain::resize: cannot resize while image is acquired");
        if (w == 0 || h == 0) LogError("[Validation] SwapChain::resize: dimensions are zero");
        return inner_->Resize(w, h);
    }

    SwapChain* inner() const { return inner_; }

private:
    SwapChain* inner_;
    bool       imageAcquired_ = false;
};

} // namespace raptor::rhi::validation
