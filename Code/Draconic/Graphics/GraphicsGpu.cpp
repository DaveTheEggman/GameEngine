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
#ifdef DRACONIC_HAS_VULKAN
import draconic.rhi.vk;
#endif
#ifdef DRACONIC_HAS_DX12
import draconic.rhi.dx12;
#endif
#ifdef DRACONIC_HAS_WEBGPU
import draconic.rhi.webgpu;
#endif
import draconic.rhi.validation;
import draconic.graphics;
import draconic.graphics.null; // Null backend delegation

namespace core = draconic::core;
namespace rhi = draconic::rhi;

namespace draconic::graphics
{
    core::Result<core::UniquePtr<GraphicsDevice>>
    CreateGraphicsDevice(const GraphicsDeviceDesc& desc)
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
#ifdef DRACONIC_HAS_VULKAN
            rhi::vk::VkBackendDesc bd{};
            bd.enableValidation = desc.enableValidation;
            if (!rhi::vk::CreateBackend(bd, raw).IsOk())
            {
                return core::Err(core::ErrorCode::Unknown);
            }
#else
            return core::Err(core::ErrorCode::Unknown); // Vulkan RHI not built (e.g. web)
#endif
            break;
        }
        case BackendType::DX12:
        {
#ifdef DRACONIC_HAS_DX12
            rhi::dx12::DxBackendDesc bd{};
            bd.enableValidation = desc.enableValidation;
            if (!rhi::dx12::CreateDxBackend(bd, raw).IsOk())
            {
                return core::Err(core::ErrorCode::Unknown);
            }
#else
            return core::Err(core::ErrorCode::Unknown);
#endif
            break;
        }
        case BackendType::WebGPU:
        {
#ifdef DRACONIC_HAS_WEBGPU
            rhi::webgpu::WebGpuBackendDesc bd{};
            if (!rhi::webgpu::CreateBackend(bd, raw).IsOk())
            {
                rhi::LogError("CreateGraphicsDevice: WebGPU CreateBackend failed");
                return core::Err(core::ErrorCode::Unknown);
            }
#else
            return core::Err(core::ErrorCode::Unknown);
#endif
            break;
        }
        case BackendType::Null:
            break; // handled above
        }

        rhi::Backend* backend =
            desc.enableValidation ? rhi::validation::CreateValidatedBackend(raw) : raw;
        return GraphicsDevice::FromBackend(backend, desc.framesInFlight, desc.requiredFeatures);
    }
}
