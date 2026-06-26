// Raptor::RuntimeGraphicsNull — the `raptor.runtime.graphics.null` module.
//
// Headless GraphicsDevice factory over the Null RHI backend (no GPU). For CI,
// servers, and tests, and the reference for what a real backend provides. Kept in
// its own module so the core host (raptor.runtime.graphics) imports only the base
// RHI — importing a backend module into the core interface trips GCC's module
// reader, and keeps the host GPU-backend-agnostic.

module;
#include "Core/Prelude.h"

export module raptor.runtime.graphics.null;

import raptor.core;
import raptor.rhi;
import raptor.rhi.null;
import raptor.runtime.graphics;

namespace rc = raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::runtime
{
    // Create a headless GraphicsDevice backed by the Null RHI. No Vulkan required.
    rc::Result<rc::UniquePtr<GraphicsDevice>> CreateNullGraphicsDevice(rc::u32 framesInFlight = 2)
    {
        rhi::Backend* raw = nullptr;
        if (!rhi::null::CreateNullBackend(raw).IsOk()) { return rc::Err(rc::ErrorCode::Unknown); }
        return GraphicsDevice::FromBackend(raw, framesInFlight);
    }
}
