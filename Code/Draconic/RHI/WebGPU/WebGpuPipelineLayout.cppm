/// draconic.rhi.webgpu:pipeline_layout - PipelineLayout over WGPUPipelineLayout.
///
/// Push constants map to WebGPU IMMEDIATES: the layout declares immediateSize (the
/// max of the RHI's declared ranges), the encoders call SetImmediates. Real on
/// wgpu-native today (WGPUNativeFeature_Immediates, requested at device creation);
/// the standard header already carries the field, so browsers follow when the
/// immediates proposal ships there.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:pipeline_layout;

import draconic.core;
import draconic.rhi;
import :api;
import :conversions;
import :bind_group_layout;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuPipelineLayout final : public PipelineLayout
    {
    public:
        Status Initialize(const WebGpuApi& api, WGPUDevice device,
                          const PipelineLayoutDesc& layoutDesc, bool immediatesSupported)
        {
            m_api = &api;

            Array<WGPUBindGroupLayout> layouts;
            for (BindGroupLayout* layout : layoutDesc.bindGroupLayouts)
            {
                if (layout == nullptr)
                {
                    return ErrorCode::InvalidArgument;
                }
                layouts.PushBack(static_cast<WebGpuBindGroupLayout*>(layout)->Handle());
            }

            u32 immediateSize = 0;
            for (const PushConstantRange& range : layoutDesc.pushConstantRanges)
            {
                const u32 end = range.offset + range.size;
                immediateSize = end > immediateSize ? end : immediateSize;
            }
            if (immediateSize > 0 && !immediatesSupported)
            {
                return ErrorCode::NotSupported; // no immediates on this device/runtime
            }

            WGPUPipelineLayoutDescriptor wgpuDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            wgpuDesc.label = ToWgpuStringView(layoutDesc.label);
            wgpuDesc.bindGroupLayoutCount = layouts.Size();
            wgpuDesc.bindGroupLayouts = layouts.Data();
            wgpuDesc.immediateSize = immediateSize;
            m_layout = api.wgpuDeviceCreatePipelineLayout(device, &wgpuDesc);
            return m_layout != nullptr ? Status(ErrorCode::Ok) : Status(ErrorCode::Unknown);
        }

        void Release()
        {
            if (m_layout != nullptr)
            {
                m_api->wgpuPipelineLayoutRelease(m_layout);
                m_layout = nullptr;
            }
        }

        [[nodiscard]] WGPUPipelineLayout Handle() const { return m_layout; }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPUPipelineLayout m_layout = nullptr;
    };
}
