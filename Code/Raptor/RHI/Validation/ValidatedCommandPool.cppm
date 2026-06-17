/// Validation wrapper for CommandPool.
/// Ported from Sedulous.RHI.Validation/ValidatedCommandPool.bf.

export module raptor.rhi.validation:validated_command_pool;

import raptor.core;
import raptor.rhi;
import :validated_command_encoder;

export namespace raptor::rhi::validation {

class ValidatedCommandPool : public CommandPool {
public:
    explicit ValidatedCommandPool(CommandPool* inner) : inner_(inner) {}

    Status createEncoder(CommandEncoder*& out) override {
        CommandEncoder* innerEnc = nullptr;
        Status r = inner_->createEncoder(innerEnc);
        if (r != ErrorCode::Ok || !innerEnc) { out = nullptr; return r; }
        out = new ValidatedCommandEncoder(innerEnc);
        return ErrorCode::Ok;
    }

    void destroyEncoder(CommandEncoder*& encoder) override {
        if (!encoder) return;
        auto* ve = dynamic_cast<ValidatedCommandEncoder*>(encoder);
        if (ve) {
            CommandEncoder* innerEnc = ve->inner();
            inner_->destroyEncoder(innerEnc);
            delete ve;
        } else {
            inner_->destroyEncoder(encoder);
        }
        encoder = nullptr;
    }

    void reset() override { inner_->reset(); }

    CommandPool* inner() const { return inner_; }

private:
    CommandPool* inner_;
};

} // namespace raptor::rhi::validation
