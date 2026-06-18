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
}
