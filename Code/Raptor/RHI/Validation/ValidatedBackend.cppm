/// Validation wrapper for Backend.
/// Ported from Sedulous.RHI.Validation/ValidatedBackend.bf.

module;
#include "Core/Prelude.h"


export module raptor.rhi.validation:validated_backend;

import raptor.core;
import raptor.rhi;
import :validated_adapter;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedBackend : public Backend {
public:
    explicit ValidatedBackend(Backend* inner) : inner_(inner) {
        isInitialized = inner->isInitialized;
    }

    Span<Adapter* const> EnumerateAdapters() override {
        if (!inner_->isInitialized) {
            LogError("[Validation] enumerateAdapters: backend not initialized");
            return {};
        }

        if (adapterWrappers_.IsEmpty()) {
            auto innerAdapters = inner_->EnumerateAdapters();
            adapterWrappers_.Reserve(innerAdapters.Size());
            adapterPtrs_.Reserve(innerAdapters.Size());
            for (usize i = 0; i < innerAdapters.Size(); ++i) {
                auto* w = CreateValidatedAdapter(innerAdapters[i]);
                adapterWrappers_.PushBack(w);
                adapterPtrs_.PushBack(w);
            }
        }
        return Span<Adapter* const>(adapterPtrs_.Data(), adapterPtrs_.Size());
    }

    Status CreateSurface(void* windowHandle, void* displayHandle, Surface*& out) override {
        if (!windowHandle) {
            LogError("[Validation] createSurface: windowHandle is null");
            out = nullptr;
            return ErrorCode::InvalidArgument;
        }
        return inner_->CreateSurface(windowHandle, displayHandle, out);
    }

    void Destroy() override {
        for (auto* w : adapterWrappers_) delete w;
        adapterWrappers_.Clear();
        adapterPtrs_.Clear();
        inner_->Destroy();
        delete this;
    }

    Backend* inner() const { return inner_; }

private:
    static ValidatedAdapter* CreateValidatedAdapter(Adapter* inner);

    Backend* inner_;
    Array<ValidatedAdapter*> adapterWrappers_;
    Array<Adapter*>          adapterPtrs_;
};

Backend* CreateValidatedBackend(Backend* inner) {
    if (!inner) { LogError("[Validation] CreateValidatedBackend: inner is null"); return nullptr; }
    return new ValidatedBackend(inner);
}

} // namespace raptor::rhi::validation
