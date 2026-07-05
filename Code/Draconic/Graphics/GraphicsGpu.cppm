// Draconic::GraphicsGpu — the `draconic.graphics.gpu` module.
//
// The GPU-backend factory for GraphicsDevice: turns a GraphicsDeviceDesc into a
// live device on Vulkan or DX12 (validation-wrapped on request), then delegates
// the backend-agnostic bring-up to GraphicsDevice::FromBackend. This is the only
// Vulkan-coupled part of the render host, kept separate so the host types — and
// everything built on them (Application, the UI, the renderer) — stay GPU-backend
// agnostic and build headlessly. Null is delegated to GraphicsDevice::CreateNull.

module;
#include "Core/Prelude.h"

export module draconic.graphics.gpu;

import draconic.core;
import draconic.rhi;
import draconic.rhi.vk;
#ifdef DRACONIC_HAS_DX12
import draconic.rhi.dx12;
#endif
import draconic.rhi.validation;
import draconic.graphics;
import draconic.graphics.null;   // Null backend delegation

namespace core = draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::graphics
{
    // Create a GraphicsDevice for the requested backend. Returns an error if the
    // backend is unavailable (e.g. DX12 off this platform) or bring-up fails.
    core::Result<core::UniquePtr<GraphicsDevice>> CreateGraphicsDevice(const GraphicsDeviceDesc& desc)
    {
        if (desc.backend == BackendType::Null)
        {
            return CreateNullGraphicsDevice(desc.framesInFlight);
        }

        rhi::Backend* raw = nullptr;
        switch (desc.backend)
        {
            case BackendType::Vulkan:
            {
                rhi::vk::VkBackendDesc bd{};
                bd.enableValidation = desc.enableValidation;
                if (!rhi::vk::CreateBackend(bd, raw).IsOk()) { return core::Err(core::ErrorCode::Unknown); }
                break;
            }
            case BackendType::DX12:
            {
#ifdef DRACONIC_HAS_DX12
                rhi::dx12::DxBackendDesc bd{};
                bd.enableValidation = desc.enableValidation;
                if (!rhi::dx12::CreateDxBackend(bd, raw).IsOk()) { return core::Err(core::ErrorCode::Unknown); }
#else
                return core::Err(core::ErrorCode::Unknown);
#endif
                break;
            }
            case BackendType::Null:
                break;  // handled above
        }

        rhi::Backend* backend = desc.enableValidation ? rhi::validation::CreateValidatedBackend(raw) : raw;
        return GraphicsDevice::FromBackend(backend, desc.framesInFlight, desc.requiredFeatures);
    }
}
