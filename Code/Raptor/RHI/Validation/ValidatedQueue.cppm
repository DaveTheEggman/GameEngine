/// Validation wrapper for Queue.
/// Ported from Sedulous.RHI.Validation/ValidatedQueue.bf.

module;

#include <vector>

export module raptor.rhi.validation:validated_queue;

import raptor.core;
import raptor.rhi;
import :validated_fence;
import :validated_transfer_batch;

export namespace raptor::rhi::validation {

class ValidatedQueue : public Queue {
public:
    explicit ValidatedQueue(Queue* inner) : inner_(inner) { queueType = inner->queueType; }

    void submit(Span<CommandBuffer* const> cmdBufs) override {
        for (usize i = 0; i < cmdBufs.count(); ++i)
            if (!cmdBufs[i]) logErrorf("[Validation] Queue::submit: commandBuffer[%zu] is null", i);
        inner_->submit(cmdBufs);
    }

    void submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) override {
        if (!signalFence) { logError("[Validation] Queue::submit: signalFence is null"); return; }
        auto* vf = dynamic_cast<ValidatedFence*>(signalFence);
        Fence* innerFence = vf ? vf->inner() : signalFence;
        if (vf) vf->trackSignal(signalValue);
        inner_->submit(cmdBufs, innerFence, signalValue);
    }

    void submit(Span<CommandBuffer* const> cmdBufs,
                Span<Fence* const> waitFences, Span<const u64> waitValues,
                Fence* signalFence, u64 signalValue) override {
        if (waitFences.count() != waitValues.count())
            logError("[Validation] Queue::submit: waitFences and waitValues count mismatch");
        // Unwrap validated fences for both wait and signal.
        std::vector<Fence*> innerWait(waitFences.count());
        for (usize i = 0; i < waitFences.count(); ++i) {
            auto* vw = dynamic_cast<ValidatedFence*>(waitFences[i]);
            innerWait[i] = vw ? vw->inner() : waitFences[i];
        }
        auto* vf = dynamic_cast<ValidatedFence*>(signalFence);
        Fence* innerSignal = vf ? vf->inner() : signalFence;
        if (vf) vf->trackSignal(signalValue);
        inner_->submit(cmdBufs, Span<Fence* const>(innerWait.data(), innerWait.size()), waitValues, innerSignal, signalValue);
    }

    void waitIdle() override { inner_->waitIdle(); }

    Status createTransferBatch(TransferBatch*& out) override {
        TransferBatch* innerBatch = nullptr;
        Status r = inner_->createTransferBatch(innerBatch);
        if (r != ErrorCode::Ok || !innerBatch) { out = nullptr; return r; }
        out = new ValidatedTransferBatch(innerBatch);
        return ErrorCode::Ok;
    }

    void destroyTransferBatch(TransferBatch*& batch) override {
        if (!batch) return;
        auto* vt = dynamic_cast<ValidatedTransferBatch*>(batch);
        if (vt) {
            TransferBatch* innerBatch = vt->inner();
            inner_->destroyTransferBatch(innerBatch);
            delete vt;
        } else {
            inner_->destroyTransferBatch(batch);
        }
        batch = nullptr;
    }

    f32 timestampPeriod() const override { return inner_->timestampPeriod(); }

    Queue* inner() const { return inner_; }

private:
    Queue* inner_;
};

} // namespace raptor::rhi::validation
