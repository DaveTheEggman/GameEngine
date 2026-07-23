/// Validation wrapper for CommandPool.
/// Ported from Sedulous.RHI.Validation/ValidatedCommandPool.bf.

module;
#include "Core/Prelude.h"

export module draconic.rhi.validation:validated_command_pool;

import draconic.core;
import draconic.rhi;
import :validated_command_encoder;

using namespace draconic::core;

export namespace draconic::rhi::validation
{

    class ValidatedCommandPool : public CommandPool
    {
    public:
        explicit ValidatedCommandPool(CommandPool* inner, IAllocator& allocator)
            : m_inner(inner), m_allocator(allocator)
        {
        }

        Status CreateEncoder(CommandEncoder*& out) override
        {
            CommandEncoder* innerEnc = nullptr;
            Status r = m_inner->CreateEncoder(innerEnc);
            if (r != ErrorCode::Ok || !innerEnc)
            {
                out = nullptr;
                return r;
            }
            out = m_allocator.New<ValidatedCommandEncoder>(innerEnc, m_allocator);
            return ErrorCode::Ok;
        }

        void DestroyEncoder(CommandEncoder*& encoder) override
        {
            if (!encoder)
                return;
            auto* ve = static_cast<ValidatedCommandEncoder*>(encoder);
            if (ve)
            {
                CommandEncoder* innerEnc = ve->inner();
                m_inner->DestroyEncoder(innerEnc);
                m_allocator.Delete(ve);
            }
            else
            {
                m_inner->DestroyEncoder(encoder);
            }
            encoder = nullptr;
        }

        void Reset() override { m_inner->Reset(); }

        CommandPool* inner() const { return m_inner; }

    private:
        CommandPool* m_inner;
        IAllocator& m_allocator;
    };

} // namespace draconic::rhi::validation
