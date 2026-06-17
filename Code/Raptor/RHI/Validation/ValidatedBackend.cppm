/// Validation wrapper for Backend.
/// Ported from Sedulous.RHI.Validation/ValidatedBackend.bf.

module;

#include <vector>

export module raptor.rhi.validation:validated_backend;

import raptor.core;
import raptor.rhi;
import :validated_adapter;

export namespace raptor::rhi::validation {

class ValidatedBackend : public Backend {
public:
    explicit ValidatedBackend(Backend* inner) : inner_(inner) {
        isInitialized = inner->isInitialized;
    }

    Span<Adapter* const> enumerateAdapters() override {
        if (!inner_->isInitialized) {
            logError("[Validation] enumerateAdapters: backend not initialized");
            return {};
        }

        if (adapterWrappers_.empty()) {
            auto innerAdapters = inner_->enumerateAdapters();
            adapterWrappers_.reserve(innerAdapters.count());
            adapterPtrs_.reserve(innerAdapters.count());
            for (usize i = 0; i < innerAdapters.count(); ++i) {
                auto* w = createValidatedAdapter(innerAdapters[i]);
                adapterWrappers_.push_back(w);
                adapterPtrs_.push_back(w);
            }
        }
        return Span<Adapter* const>(adapterPtrs_.data(), adapterPtrs_.size());
    }

    Status createSurface(void* windowHandle, void* displayHandle, Surface*& out) override {
        if (!windowHandle) {
            logError("[Validation] createSurface: windowHandle is null");
            out = nullptr;
            return ErrorCode::InvalidArgument;
        }
        return inner_->createSurface(windowHandle, displayHandle, out);
    }

    void destroy() override {
        for (auto* w : adapterWrappers_) delete w;
        adapterWrappers_.clear();
        adapterPtrs_.clear();
        inner_->destroy();
        delete this;
    }

    Backend* inner() const { return inner_; }

private:
    static ValidatedAdapter* createValidatedAdapter(Adapter* inner);

    Backend* inner_;
    std::vector<ValidatedAdapter*> adapterWrappers_;
    std::vector<Adapter*>          adapterPtrs_;
};

Backend* createValidatedBackend(Backend* inner) {
    if (!inner) { logError("[Validation] createValidatedBackend: inner is null"); return nullptr; }
    return new ValidatedBackend(inner);
}

} // namespace raptor::rhi::validation
