// Draconic::GraphicsGpu - implementation unit for `draconic.graphics.gpu`.
//
// All the backend-coupled code (Vulkan / DX12 / validation) lives here rather than
// in the interface unit. Because these imports sit in the implementation unit,
// consumers of draconic.graphics.gpu never load the backend BMIs at compile time -
// they only link the backends. This is the "only Vulkan-coupled part" of the render
// host, kept off every consumer's module closure.

module;
#include "Core/Prelude.h"

module draconic.graphics.gpu;

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

namespace draconic::graphics
{
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
