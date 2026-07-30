/// draconic.rhi.webgpu:compute_pass_encoder - ComputePassEncoder over WGPUComputePassEncoder.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:compute_pass_encoder;

import draconic.core;
import draconic.rhi;
import :api;
import :bind_group;
import :buffer;
import :compute_pipeline;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuComputePassEncoder final : public ComputePassEncoder
    {
    public:
        void Begin(const WebGpuApi& api, WGPUComputePassEncoder encoder)
        {
            m_api = &api;
            m_encoder = encoder;
        }

        void SetPipeline(ComputePipeline* pipeline) override
        {
            m_api->wgpuComputePassEncoderSetPipeline(
                m_encoder, static_cast<WebGpuComputePipeline*>(pipeline)->Handle());
        }

        void SetBindGroup(u32 index, BindGroup* group, Span<const u32> dynamicOffsets) override
        {
            m_api->wgpuComputePassEncoderSetBindGroup(
                m_encoder, index, static_cast<WebGpuBindGroup*>(group)->Handle(),
                dynamicOffsets.Size(), dynamicOffsets.Data());
        }

        void SetPushConstants(ShaderStage, u32 offset, u32 size, const void* data) override
        {
            m_api->wgpuComputePassEncoderSetImmediates(m_encoder, offset, data, size);
        }

        void Dispatch(u32 x, u32 y, u32 z) override
        {
            m_api->wgpuComputePassEncoderDispatchWorkgroups(m_encoder, x, y, z);
        }

        void DispatchIndirect(Buffer* buffer, u64 offset) override
        {
            m_api->wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                m_encoder, static_cast<WebGpuBuffer*>(buffer)->Handle(), offset);
        }

        void ComputeBarrier() override
        {
            // WebGPU tracks hazards automatically - dispatch ordering within a pass is
            // already dependency-correct.
        }

        void WriteTimestamp(QuerySet*, u32) override
        {
            // Pass-interior timestamps have no WebGPU shape (begin/end-of-pass only).
        }

        void End() override
        {
            m_api->wgpuComputePassEncoderEnd(m_encoder);
            m_api->wgpuComputePassEncoderRelease(m_encoder);
            m_encoder = nullptr;
        }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPUComputePassEncoder m_encoder = nullptr;
    };
}
