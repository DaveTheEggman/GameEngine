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
        Status Initialize(const WebGpuApi& api, WGPUAdapter adapter, WGPUDevice device,
                          WebGpuSurface* surface, const SwapChainDesc& swapDesc)
        {
            m_api = &api;
            m_adapter = adapter;
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
#if DRACONIC_PLATFORM_WEB
            // The browser presents the canvas automatically once the requestAnimationFrame
            // callback (the web runner's frame) returns; emdawnwebgpu ABORTS on an explicit
            // wgpuSurfacePresent. Just drop the borrowed texture.
            DropCurrent();
            return ErrorCode::Ok;
#else
            const WGPUStatus status = m_api->wgpuSurfacePresent(m_surface->Handle());
            DropCurrent();
            return status == WGPUStatus_Success ? Status(ErrorCode::Ok)
                                                : Status(ErrorCode::Unknown);
#endif
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
        // The non-sRGB companion of an sRGB color format (identity otherwise). A WebGPU canvas
        // context only accepts a non-sRGB config format, so an sRGB swapchain is configured with
        // this base format plus the sRGB format as a viewFormat.
        static TextureFormat BaseColorFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::BGRA8UnormSrgb:
                return TextureFormat::BGRA8Unorm;
            case TextureFormat::RGBA8UnormSrgb:
                return TextureFormat::RGBA8Unorm;
            default:
                return format;
            }
        }

        Status Configure(u32 width, u32 height)
        {
            m_width = width;
            m_height = height;
            if (width == 0 || height == 0)
            {
                // A zero-size surface is illegal (WebGPU errors "size is zero"); a canvas hits this
                // transiently across a fullscreen/minimize transition. Skip configuring - the last
                // valid configuration stays, AcquireNextImage fails, the host skips the frame, and
                // the resize pump reconfigures once a real size arrives.
                return ErrorCode::Ok;
            }

            WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
            config.device = m_device;
            // The pipeline's final hop COPIES the tonemapped output into the
            // backbuffer, so the surface needs CopyDst alongside RenderAttachment
            // (universally supported by wgpu surfaces).
            config.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
            config.width = width;
            config.height = height;
            config.presentMode = SupportedPresentMode(ToWgpuPresentMode(m_presentMode));

#if DRACONIC_PLATFORM_WEB
            // A WebGPU canvas context does not accept an sRGB config format, so configure with the
            // base format and expose the sRGB format as a viewFormat. AcquireNextImage then creates
            // the per-frame view in m_format (the sRGB format), so the engine's default sRGB
            // swapchain renders correctly with no app-side change. Desktop wgpu-native accepts sRGB
            // config formats directly, so it is left exactly as before.
            const TextureFormat baseFormat = BaseColorFormat(m_format);
            config.format = ToWgpuTextureFormat(baseFormat);
            WGPUTextureFormat viewFormat = ToWgpuTextureFormat(m_format);
            if (baseFormat != m_format)
            {
                config.viewFormatCount = 1;
                config.viewFormats = &viewFormat;
            }
#else
            config.format = ToWgpuTextureFormat(m_format);
#endif

            m_api->wgpuSurfaceConfigure(m_surface->Handle(), &config);
            m_configured = true;
            m_width = width;
            m_height = height;
            return ErrorCode::Ok;
        }

        /// The requested mode when the surface offers it, else the closest match
        /// (Immediate/Mailbox degrade toward each other, everything else to Fifo -
        /// the only mode WebGPU guarantees).
        WGPUPresentMode SupportedPresentMode(WGPUPresentMode requested)
        {
            WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
            if (m_api->wgpuSurfaceGetCapabilities(m_surface->Handle(), m_adapter,
                                                  &capabilities) != WGPUStatus_Success)
            {
                return WGPUPresentMode_Fifo;
            }
            const auto supported = [&](WGPUPresentMode mode)
            {
                for (usize i = 0; i < capabilities.presentModeCount; ++i)
                {
                    if (capabilities.presentModes[i] == mode)
                    {
                        return true;
                    }
                }
                return false;
            };
            WGPUPresentMode chosen = WGPUPresentMode_Fifo;
            if (supported(requested))
            {
                chosen = requested;
            }
            else if ((requested == WGPUPresentMode_Immediate ||
                      requested == WGPUPresentMode_Mailbox) &&
                     supported(WGPUPresentMode_Mailbox))
            {
                chosen = WGPUPresentMode_Mailbox;
            }
            m_api->wgpuSurfaceCapabilitiesFreeMembers(capabilities);
            return chosen;
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
        WGPUAdapter m_adapter = nullptr;
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
