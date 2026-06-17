/// Abstract GPU command queue. Handles command submission, fence
/// synchronization, and transfer batch creation.

export module raptor.rhi:queue;

import raptor.core;
import :enums;
import :resources;

using namespace raptor::core;

export namespace raptor::rhi {

class TransferBatch;

class Queue {
public:
    virtual ~Queue() = default;

    /// The type of work this queue supports.
    QueueType queueType = QueueType::Graphics;

    /// Submit command buffers for execution.
    virtual void submit(Span<CommandBuffer* const> commandBuffers) = 0;

    /// Submit with fence signaling.
    virtual void submit(Span<CommandBuffer* const> commandBuffers,
                        Fence* signalFence, u64 signalValue) = 0;

    /// Submit with full synchronization: wait on fences, then signal.
    virtual void submit(Span<CommandBuffer* const> commandBuffers,
                        Span<Fence* const> waitFences, Span<const u64> waitValues,
                        Fence* signalFence, u64 signalValue) = 0;

    /// Block until all submitted work on this queue completes.
    virtual void waitIdle() = 0;

    /// Create a transfer batch for batching CPU→GPU uploads.
    virtual Status createTransferBatch(TransferBatch*& out) = 0;
    /// Destroy a transfer batch.
    virtual void destroyTransferBatch(TransferBatch*& batch) = 0;

    /// Timestamp period in nanoseconds per tick.
    [[nodiscard]] virtual f32 timestampPeriod() const = 0;
};

} // namespace raptor::rhi
