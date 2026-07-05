// Draconic::GraphicsNull - the `draconic.graphics.null` module.
//
// Headless GraphicsDevice factory over the Null RHI backend (no GPU). For CI,
// servers, and tests, and the reference for what a real backend provides. Kept in
// its own module so the core host (draconic.graphics) imports only the base
// RHI - importing a backend module into the core interface trips GCC's module
// reader, and keeps the host GPU-backend-agnostic.

module;
#include "Core/Prelude.h"

export module draconic.graphics.null;

import draconic.core;
import draconic.rhi;
import draconic.rhi.null;
import draconic.graphics;

namespace core = draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::graphics
{
    // Create a headless GraphicsDevice backed by the Null RHI. No Vulkan required.
    core::Result<core::UniquePtr<GraphicsDevice>> CreateNullGraphicsDevice(core::u32 framesInFlight = 2)
    {
        rhi::Backend* raw = nullptr;
        if (!rhi::null::CreateNullBackend(raw).IsOk()) { return core::Err(core::ErrorCode::Unknown); }
        return GraphicsDevice::FromBackend(raw, framesInFlight);
    }
}
