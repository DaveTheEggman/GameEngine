// Draconic GUI - :mutation_queue partition
//
// MutationQueue: deferred operations drained at a safe sync point (end of the SceneNode
// update). This is how tree edits made mid-traversal (notably Node::Close) are applied
// without mutating the tree while it is being walked - eepp's SceneNode close-queue role,
// generalized to arbitrary deferred ops (same shape as draconic.ui's MutationQueue).

module;
#include "Draconic.Core/Prelude.h"

export module draconic.gui:mutation_queue;

import draconic.core; // Function, Array, Move

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class MutationQueue
    {
    public:
        void Enqueue(core::Function<void()> op) { m_ops.PushBack(core::Move(op)); }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_ops.Size() == 0; }

        // Run and clear all queued ops. Ops enqueued during draining run on the next drain.
        void Drain()
        {
            Array<core::Function<void()>> batch = core::Move(m_ops);
            m_ops = Array<core::Function<void()>>{};
            for (core::Function<void()>& op : batch)
                op();
        }

    private:
        Array<core::Function<void()>> m_ops;
    };
}
