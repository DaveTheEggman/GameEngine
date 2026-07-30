/// draconic.rhi.webgpu:render_pass_encoder - RenderPassEncoder over WGPURenderPassEncoder.
///
/// SetPushConstants maps to SetImmediates (real - see :pipeline_layout). Multi-draw
/// indirect unrolls into single indirect draws (core WebGPU has one-draw indirect).
/// Occlusion queries require RenderPassDesc.occlusionQuerySet declared at pass
/// begin; Begin/End then carry only the index. Timestamps also ride the pass
/// descriptor (see :command_encoder).

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:render_pass_encoder;

import draconic.core;
import draconic.rhi;
import :api;
import :bind_group;
import :buffer;
import :render_pipeline;
import :render_bundle_encoder;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuRenderPassEncoder final : public RenderPassEncoder
    {
    public:
        void Begin(const WebGpuApi& api, WGPURenderPassEncoder encoder)
        {
            m_api = &api;
            m_encoder = encoder;
        }

        void SetPipeline(RenderPipeline* pipeline) override
        {
            m_api->wgpuRenderPassEncoderSetPipeline(
                m_encoder, static_cast<WebGpuRenderPipeline*>(pipeline)->Handle());
        }

        void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynamicOffsets) override
        {
            m_api->wgpuRenderPassEncoderSetBindGroup(
                m_encoder, index, static_cast<WebGpuBindGroup*>(group)->Handle(),
                dynamicOffsets.Size(), dynamicOffsets.Data());
        }

        void SetPushConstants(ShaderStage, u32 offset, u32 size, const void* data) override
        {
            m_api->wgpuRenderPassEncoderSetImmediates(m_encoder, offset, data, size);
        }

        void SetVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override
        {
            m_api->wgpuRenderPassEncoderSetVertexBuffer(
                m_encoder, slot, static_cast<WebGpuBuffer*>(buffer)->Handle(), offset,
                WGPU_WHOLE_SIZE);
        }

        void SetIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override
        {
            m_api->wgpuRenderPassEncoderSetIndexBuffer(
                m_encoder, static_cast<WebGpuBuffer*>(buffer)->Handle(),
                format == IndexFormat::UInt16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
                offset, WGPU_WHOLE_SIZE);
        }

        void SetViewport(f32 x, f32 y, f32 width, f32 height, f32 minDepth, f32 maxDepth) override
        {
            m_api->wgpuRenderPassEncoderSetViewport(m_encoder, x, y, width, height, minDepth,
                                                    maxDepth);
        }

        void SetScissor(i32 x, i32 y, u32 width, u32 height) override
        {
            m_api->wgpuRenderPassEncoderSetScissorRect(m_encoder, static_cast<u32>(x),
                                                       static_cast<u32>(y), width, height);
        }

        void SetBlendConstant(f32 r, f32 g, f32 b, f32 a) override
        {
            const WGPUColor color{r, g, b, a};
            m_api->wgpuRenderPassEncoderSetBlendConstant(m_encoder, &color);
        }

        void SetStencilReference(u32 reference) override
        {
            m_api->wgpuRenderPassEncoderSetStencilReference(m_encoder, reference);
        }

        void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override
        {
            m_api->wgpuRenderPassEncoderDraw(m_encoder, vertexCount, instanceCount, firstVertex,
                                             firstInstance);
        }

        void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex,
                         u32 firstInstance) override
        {
            m_api->wgpuRenderPassEncoderDrawIndexed(m_encoder, indexCount, instanceCount,
                                                    firstIndex, baseVertex, firstInstance);
        }

        void DrawIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override
        {
            const WGPUBuffer handle = static_cast<WebGpuBuffer*>(buffer)->Handle();
            for (u32 i = 0; i < drawCount; ++i) // core WebGPU: one draw per indirect call
            {
                m_api->wgpuRenderPassEncoderDrawIndirect(m_encoder, handle,
                                                         offset + static_cast<u64>(i) * stride);
            }
        }

        void DrawIndexedIndirect(Buffer* buffer, u64 offset, u32 drawCount, u32 stride) override
        {
            const WGPUBuffer handle = static_cast<WebGpuBuffer*>(buffer)->Handle();
            for (u32 i = 0; i < drawCount; ++i)
            {
                m_api->wgpuRenderPassEncoderDrawIndexedIndirect(
                    m_encoder, handle, offset + static_cast<u64>(i) * stride);
            }
        }

        void ExecuteBundles(Span<RenderBundle* const> bundles) override
        {
            WGPURenderBundle handles[16];
            usize count = 0;
            for (RenderBundle* bundle : bundles)
            {
                if (count == 16)
                {
                    m_api->wgpuRenderPassEncoderExecuteBundles(m_encoder, count, handles);
                    count = 0;
                }
                handles[count++] = static_cast<WebGpuRenderBundle*>(bundle)->Handle();
            }
            if (count > 0)
            {
                m_api->wgpuRenderPassEncoderExecuteBundles(m_encoder, count, handles);
            }
        }

        void WriteTimestamp(QuerySet*, u32) override
        {
            // Pass-interior timestamps have no WebGPU shape; begin/end-of-pass writes
            // ride the pass descriptor (RenderPassDesc.timestampQuerySet).
        }

        void BeginOcclusionQuery(QuerySet*, u32 index) override
        {
            // The set itself was declared at pass begin (RenderPassDesc.
            // occlusionQuerySet); WebGPU only takes the index here.
            m_api->wgpuRenderPassEncoderBeginOcclusionQuery(m_encoder, index);
        }
        void EndOcclusionQuery(QuerySet*, u32) override
        {
            m_api->wgpuRenderPassEncoderEndOcclusionQuery(m_encoder);
        }

        void End() override
        {
            m_api->wgpuRenderPassEncoderEnd(m_encoder);
            m_api->wgpuRenderPassEncoderRelease(m_encoder);
            m_encoder = nullptr;
        }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPURenderPassEncoder m_encoder = nullptr;
    };
}
