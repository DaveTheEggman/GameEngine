/// Validation wrapper for Queue.
/// Ported from Sedulous.RHI.Validation/ValidatedQueue.bf.

module;
#include "Core/Prelude.h"


export module raptor.rhi.validation:validated_queue;

import raptor.core;
import raptor.rhi;
import :validated_fence;
import :validated_transfer_batch;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedQueue : public Queue {
public:
    explicit ValidatedQueue(Queue* inner) : m_inner(inner) { queueType = inner->queueType; }

    void Submit(Span<CommandBuffer* const> cmdBufs) override {
        for (usize i = 0; i < cmdBufs.Size(); ++i)
            if (!cmdBufs[i]) LogErrorf("[Validation] Queue::submit: commandBuffer[%zu] is null", i);
        m_inner->Submit(cmdBufs);
    }

    void Submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) override {
        if (!signalFence) { LogError("[Validation] Queue::submit: signalFence is null"); return; }
        auto* vf = static_cast<ValidatedFence*>(signalFence);
        Fence* innerFence = vf ? vf->inner() : signalFence;
        if (vf) vf->trackSignal(signalValue);
        m_inner->Submit(cmdBufs, innerFence, signalValue);
    }

    void Submit(Span<CommandBuffer* const> cmdBufs,
                Span<Fence* const> waitFences, Span<const u64> waitValues,
                Fence* signalFence, u64 signalValue) override {
        if (waitFences.Size() != waitValues.Size())
            LogError("[Validation] Queue::submit: waitFences and waitValues count mismatch");
        // Unwrap validated fences for both wait and signal.
        Array<Fence*> innerWait(waitFences.Size());
        for (usize i = 0; i < waitFences.Size(); ++i) {
            auto* vw = static_cast<ValidatedFence*>(waitFences[i]);
            innerWait[i] = vw ? vw->inner() : waitFences[i];
        }
        auto* vf = static_cast<ValidatedFence*>(signalFence);
        Fence* innerSignal = vf ? vf->inner() : signalFence;
        if (vf) vf->trackSignal(signalValue);
        m_inner->Submit(cmdBufs, Span<Fence* const>(innerWait.Data(), innerWait.Size()), waitValues, innerSignal, signalValue);
    }

    void WaitIdle() override { m_inner->WaitIdle(); }

    Status CreateTransferBatch(TransferBatch*& out) override {
        TransferBatch* innerBatch = nullptr;
        Status r = m_inner->CreateTransferBatch(innerBatch);
        if (r != ErrorCode::Ok || !innerBatch) { out = nullptr; return r; }
        out = new ValidatedTransferBatch(innerBatch);
        return ErrorCode::Ok;
    }

    void DestroyTransferBatch(TransferBatch*& batch) override {
        if (!batch) return;
        auto* vt = static_cast<ValidatedTransferBatch*>(batch);
        if (vt) {
            TransferBatch* innerBatch = vt->inner();
            m_inner->DestroyTransferBatch(innerBatch);
            delete vt;
        } else {
            m_inner->DestroyTransferBatch(batch);
        }
        batch = nullptr;
    }

    f32 TimestampPeriod() const override { return m_inner->TimestampPeriod(); }

    Queue* inner() const { return m_inner; }

private:
    Queue* m_inner;
};

} // namespace raptor::rhi::validation
