/// draconic.rhi.webgpu:render_bundle_encoder - RenderBundle(+Encoder) over WGPU.
///
/// Bundles are a native WebGPU concept (the RHI's bundle-safe command subset is
/// DEFINED as "valid inside a WebGPU render bundle") - a direct mapping. The
/// RenderBundleDesc viewport extent is for backends that cannot inherit pass state;
/// WebGPU bundles inherit, so it is ignored here.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:render_bundle_encoder;

import draconic.core;
import draconic.rhi;
import :api;
import :conversions;
import :bind_group;
import :buffer;
import :render_pipeline;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuRenderBundle final : public RenderBundle
    {
    public:
        void Adopt(const WebGpuApi& api, WGPURenderBundle bundle)
        {
            m_api = &api;
            m_bundle = bundle;
        }

        void Release()
        {
            if (m_bundle != nullptr)
            {
                m_api->wgpuRenderBundleRelease(m_bundle);
                m_bundle = nullptr;
            }
        }

        [[nodiscard]] WGPURenderBundle Handle() const { return m_bundle; }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPURenderBundle m_bundle = nullptr;
    };

    class WebGpuRenderBundleEncoder final : public RenderBundleEncoder
    {
    public:
        Status Initialize(const WebGpuApi& api, WGPUDevice device, IAllocator& allocator,
                          const RenderBundleDesc& bundleDesc)
        {
            m_api = &api;
            m_allocator = &allocator;

            WGPUTextureFormat colorFormats[MaxColorAttachments] = {};
            for (u32 i = 0; i < bundleDesc.colorFormatCount; ++i)
            {
                colorFormats[i] = ToWgpuTextureFormat(bundleDesc.colorFormats[i]);
            }
            WGPURenderBundleEncoderDescriptor wgpuDesc =
                WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            wgpuDesc.label = ToWgpuStringView(bundleDesc.label);
            wgpuDesc.colorFormatCount = bundleDesc.colorFormatCount;
            wgpuDesc.colorFormats = colorFormats;
            wgpuDesc.depthStencilFormat =
                ToWgpuTextureFormat(bundleDesc.depthStencilFormat);
            wgpuDesc.depthReadOnly = bundleDesc.depthReadOnly;
            wgpuDesc.stencilReadOnly = bundleDesc.stencilReadOnly;
            wgpuDesc.sampleCount = bundleDesc.sampleCount;
            m_encoder = api.wgpuDeviceCreateRenderBundleEncoder(device, &wgpuDesc);
            return m_encoder != nullptr ? Status(ErrorCode::Ok) : Status(ErrorCode::Unknown);
        }

        void SetPipeline(RenderPipeline* pipeline) override
        {
            m_api->wgpuRenderBundleEncoderSetPipeline(
                m_encoder, static_cast<WebGpuRenderPipeline*>(pipeline)->Handle());
        }

        void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynamicOffsets) override
        {
            m_api->wgpuRenderBundleEncoderSetBindGroup(
                m_encoder, index, static_cast<WebGpuBindGroup*>(group)->Handle(),
                dynamicOffsets.Size(), dynamicOffsets.Data());
        }

        void SetPushConstants(ShaderStage, u32 offset, u32 size, const void* data) override
        {
            m_api->wgpuRenderBundleEncoderSetImmediates(m_encoder, offset, data, size);
        }

        void SetVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override
        {
            m_api->wgpuRenderBundleEncoderSetVertexBuffer(
                m_encoder, slot, static_cast<WebGpuBuffer*>(buffer)->Handle(), offset,
                WGPU_WHOLE_SIZE);
        }

        void SetIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override
        {
            m_api->wgpuRenderBundleEncoderSetIndexBuffer(
                m_encoder, static_cast<WebGpuBuffer*>(buffer)->Handle(),
                format == IndexFormat::UInt16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
                offset, WGPU_WHOLE_SIZE);
        }

        void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override
        {
            m_api->wgpuRenderBundleEncoderDraw(m_encoder, vertexCount, instanceCount,
                                               firstVertex, firstInstance);
        }

        void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex,
                         u32 firstInstance) override
        {
            m_api->wgpuRenderBundleEncoderDrawIndexed(m_encoder, indexCount, instanceCount,
                                                      firstIndex, baseVertex, firstInstance);
        }

        void DrawIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override
        {
            const WGPUBuffer handle = static_cast<WebGpuBuffer*>(buffer)->Handle();
            for (u32 i = 0; i < drawCount; ++i)
            {
                m_api->wgpuRenderBundleEncoderDrawIndirect(
                    m_encoder, handle, offset + static_cast<u64>(i) * stride);
            }
        }

        void DrawIndexedIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override
        {
            const WGPUBuffer handle = static_cast<WebGpuBuffer*>(buffer)->Handle();
            for (u32 i = 0; i < drawCount; ++i)
            {
                m_api->wgpuRenderBundleEncoderDrawIndexedIndirect(
                    m_encoder, handle, offset + static_cast<u64>(i) * stride);
            }
        }

        RenderBundle* Finish() override
        {
            WGPURenderBundleDescriptor wgpuDesc = WGPU_RENDER_BUNDLE_DESCRIPTOR_INIT;
            WGPURenderBundle handle =
                m_api->wgpuRenderBundleEncoderFinish(m_encoder, &wgpuDesc);
            m_api->wgpuRenderBundleEncoderRelease(m_encoder);
            m_encoder = nullptr;
            auto* bundle = m_allocator->New<WebGpuRenderBundle>();
            bundle->Adopt(*m_api, handle);
            m_bundles.PushBack(bundle); // owned until the encoder wrapper dies
            return bundle;
        }

        ~WebGpuRenderBundleEncoder() override
        {
            for (WebGpuRenderBundle* bundle : m_bundles)
            {
                bundle->Release();
                m_allocator->Delete(bundle);
            }
        }

    private:
        const WebGpuApi* m_api = nullptr;
        IAllocator* m_allocator = nullptr;
        WGPURenderBundleEncoder m_encoder = nullptr;
        Array<WebGpuRenderBundle*> m_bundles;
    };
}
