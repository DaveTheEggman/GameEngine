/// draconic.rhi.webgpu:shader_module - ShaderModule over WGPUShaderModule.
///
/// Desktop dev loop: the ShaderModuleDesc carries the SAME DXC-produced SPIR-V the
/// Vulkan backend consumes, ingested through wgpu-native's SPIR-V passthrough
/// (wgpuDeviceCreateShaderModuleSpirV - an EXTENSION; browsers refuse SPIR-V).
/// The browser path arrives with the shaders track's cook-time WGSL: same desc, the
/// bytes are WGSL text, ingested through the standard WGSL chained struct. The
/// discriminator is the SPIR-V magic in the first word.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:shader_module;

import draconic.core;
import draconic.rhi;
import :api;
import :conversions;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuShaderModule final : public ShaderModule
    {
    public:
        Status Initialize(const WebGpuApi& api, WGPUDevice device, const ShaderModuleDesc& desc)
        {
            m_api = &api;

            const bool isSpirv = desc.code.Size() >= 4 &&
                                 *reinterpret_cast<const u32*>(desc.code.Data()) == 0x07230203u;
            if (isSpirv)
            {
                if (api.wgpuDeviceCreateShaderModuleSpirV == nullptr)
                {
                    return ErrorCode::NotSupported; // browser: SPIR-V never ingests
                }
                WGPUShaderModuleDescriptorSpirV spirvDesc{};
                spirvDesc.label = ToWgpuStringView(desc.label);
                spirvDesc.sourceSize = static_cast<u32>(desc.code.Size() / 4);
                spirvDesc.source = reinterpret_cast<const u32*>(desc.code.Data());
                m_module = api.wgpuDeviceCreateShaderModuleSpirV(device, &spirvDesc);
            }
            else
            {
                // WGSL text (cook-time output once the shaders track lands).
                WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
                wgsl.code = WGPUStringView{reinterpret_cast<const char*>(desc.code.Data()),
                                           desc.code.Size()};
                WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
                moduleDesc.label = ToWgpuStringView(desc.label);
                moduleDesc.nextInChain = &wgsl.chain;
                m_module = api.wgpuDeviceCreateShaderModule(device, &moduleDesc);
            }
            return m_module != nullptr ? Status(ErrorCode::Ok) : Status(ErrorCode::Unknown);
        }

        void Release()
        {
            if (m_module != nullptr)
            {
                m_api->wgpuShaderModuleRelease(m_module);
                m_module = nullptr;
            }
        }

        [[nodiscard]] WGPUShaderModule Handle() const { return m_module; }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPUShaderModule m_module = nullptr;
    };
}
