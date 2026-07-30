/// draconic.rhi.webgpu:swapchain - SwapChain over the configured WGPUSurface.
///
/// WebGPU has no swapchain object: the surface is CONFIGURED (format/size/present
/// mode) and each frame borrows the current texture. AcquireNextImage wraps the
/// borrowed WGPUTexture (WrapExternal - the surface owns it) + creates its view;
/// Present hands it to the compositor and drops the borrow. There is no image
/// index in the API - a frame counter modulo bufferCount satisfies the RHI shape.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:swapchain;

import draconic.core;
import draconic.rhi;
import :api;
import :conversions;
import :surface;
import :texture;
import :texture_view;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuSwapChain final : public SwapChain
    {
    public:
        Status Initialize(const WebGpuApi& api, WGPUDevice device, WebGpuSurface* surface,
                          const SwapChainDesc& swapDesc)
        {
            m_api = &api;
            m_device = device;
            m_surface = surface;
            m_format = swapDesc.format;
            m_presentMode = swapDesc.presentMode;
            m_bufferCount = swapDesc.bufferCount;
            return Configure(swapDesc.width, swapDesc.height);
        }

        TextureFormat Format() const override { return m_format; }
        u32 Width() const override { return m_width; }
        u32 Height() const override { return m_height; }
        u32 BufferCount() const override { return m_bufferCount; }
        u32 CurrentImageIndex() const override { return m_frameIndex % m_bufferCount; }

        Status AcquireNextImage() override
        {
            DropCurrent();

            WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
            m_api->wgpuSurfaceGetCurrentTexture(m_surface->Handle(), &surfaceTexture);
            if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
            {
                if (surfaceTexture.texture != nullptr)
                {
                    m_api->wgpuTextureRelease(surfaceTexture.texture);
                }
                return ErrorCode::Unknown; // lost/outdated: host resizes + retries
            }

            TextureDesc textureDesc;
            textureDesc.format = m_format;
            textureDesc.width = m_width;
            textureDesc.height = m_height;
            textureDesc.usage = TextureUsage::RenderTarget;
            m_currentTexture.WrapExternal(*m_api, surfaceTexture.texture, textureDesc);
            m_ownedHandle = surfaceTexture.texture; // released at Present/Drop

            TextureViewDesc viewDesc;
            viewDesc.format = m_format;
            const Status viewStatus =
                m_currentView.Initialize(*m_api, &m_currentTexture, viewDesc);
            if (!viewStatus.IsOk())
            {
                DropCurrent();
                return viewStatus;
            }
            m_haveImage = true;
            ++m_frameIndex;
            return ErrorCode::Ok;
        }

        Texture* CurrentTexture() override { return m_haveImage ? &m_currentTexture : nullptr; }
        TextureView* CurrentTextureView() override
        {
            return m_haveImage ? &m_currentView : nullptr;
        }

        Status Present(Queue*) override
        {
            if (!m_haveImage)
            {
                return ErrorCode::InvalidArgument;
            }
            const WGPUStatus status = m_api->wgpuSurfacePresent(m_surface->Handle());
            DropCurrent();
            return status == WGPUStatus_Success ? Status(ErrorCode::Ok)
                                                : Status(ErrorCode::Unknown);
        }

        Status Resize(u32 width, u32 height) override
        {
            DropCurrent();
            return Configure(width, height);
        }

        void Cleanup()
        {
            DropCurrent();
            if (m_configured)
            {
                m_api->wgpuSurfaceUnconfigure(m_surface->Handle());
                m_configured = false;
            }
        }

    private:
        Status Configure(u32 width, u32 height)
        {
            WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
            config.device = m_device;
            config.format = ToWgpuTextureFormat(m_format);
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = width;
            config.height = height;
            config.presentMode = ToWgpuPresentMode(m_presentMode);
            m_api->wgpuSurfaceConfigure(m_surface->Handle(), &config);
            m_configured = true;
            m_width = width;
            m_height = height;
            return ErrorCode::Ok;
        }

        void DropCurrent()
        {
            if (!m_haveImage)
            {
                return;
            }
            m_currentView.Release();
            if (m_ownedHandle != nullptr)
            {
                m_api->wgpuTextureRelease(m_ownedHandle);
                m_ownedHandle = nullptr;
            }
            m_haveImage = false;
        }

        const WebGpuApi* m_api = nullptr;
        WGPUDevice m_device = nullptr;
        WebGpuSurface* m_surface = nullptr;
        TextureFormat m_format = TextureFormat::BGRA8UnormSrgb;
        PresentMode m_presentMode = PresentMode::Fifo;
        u32 m_width = 0;
        u32 m_height = 0;
        u32 m_bufferCount = 2;
        u32 m_frameIndex = 0;
        bool m_configured = false;
        bool m_haveImage = false;
        WebGpuTexture m_currentTexture;
        WebGpuTextureView m_currentView;
        WGPUTexture m_ownedHandle = nullptr;
    };
}
