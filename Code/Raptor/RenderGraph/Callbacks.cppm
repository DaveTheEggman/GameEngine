// Raptor::RenderGraph — :callbacks partition
//
// Pass execution callbacks. Sedulous uses Beef delegates; here they are Core
// Function objects over the RHI encoder a pass records into.

module;
#include "Core/Prelude.h"

export module raptor.rendergraph:callbacks;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rendergraph
{
    namespace rhi = raptor::rhi;

    using RenderPassExecuteCallback  = Function<void(rhi::RenderPassEncoder&)>;
    using ComputePassExecuteCallback = Function<void(rhi::ComputePassEncoder&)>;
    using CopyPassExecuteCallback    = Function<void(rhi::CommandEncoder&)>;

    // A render pass whose body is supplied by render bundles (recorded off-thread). Called with
    // the command encoder in the RECORDING state (before the render pass begins, so bundles can
    // be created) and an out-list to fill with the bundles to replay; the graph then begins the
    // pass with secondary-command-buffer contents and ExecuteBundles them in order. This is what
    // lets parallel command recording run inside the frame graph.
    using RenderBundlePassCallback   = Function<void(rhi::CommandEncoder&, Array<rhi::RenderBundle*>&)>;
}
