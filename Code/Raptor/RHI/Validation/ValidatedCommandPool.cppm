/// Validation wrapper for CommandPool.
/// Ported from Sedulous.RHI.Validation/ValidatedCommandPool.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_command_pool;

import raptor.core;
import raptor.rhi;
import :validated_command_encoder;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedCommandPool : public CommandPool {
public:
    explicit ValidatedCommandPool(CommandPool* inner) : m_inner(inner) {}

    Status CreateEncoder(CommandEncoder*& out) override {
        CommandEncoder* innerEnc = nullptr;
        Status r = m_inner->CreateEncoder(innerEnc);
        if (r != ErrorCode::Ok || !innerEnc) { out = nullptr; return r; }
        out = new ValidatedCommandEncoder(innerEnc);
        return ErrorCode::Ok;
    }

    void DestroyEncoder(CommandEncoder*& encoder) override {
        if (!encoder) return;
        auto* ve = static_cast<ValidatedCommandEncoder*>(encoder);
        if (ve) {
            CommandEncoder* innerEnc = ve->inner();
            m_inner->DestroyEncoder(innerEnc);
            delete ve;
        } else {
            m_inner->DestroyEncoder(encoder);
        }
        encoder = nullptr;
    }

    void Reset() override { m_inner->Reset(); }

    CommandPool* inner() const { return m_inner; }

private:
    CommandPool* m_inner;
};

} // namespace raptor::rhi::validation
