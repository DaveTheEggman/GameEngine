/// draconic.rhi.webgpu:buffer - Buffer over WGPUBuffer, with the Map emulation.
///
/// The RHI's Map contract is Vulkan-shaped: persistent host pointer, Unmap a formality
/// (renderer callers pair Map/Unmap around writes each frame). WebGPU forbids mapping
/// buffers that carry normal usages, so:
///   - CpuToGpu: Map returns a CPU SHADOW; Unmap flushes it with wgpuQueueWriteBuffer
///     (queue-ordered, so it lands before any later submission that reads the buffer).
///   - GpuToCpu: a genuine WebGPU mapping - MapAsync(Read) + ProcessEvents pump in Map,
///     wgpuBufferUnmap in Unmap. Usage is forced to MapRead|CopyDst (all WebGPU allows).
///   - GpuOnly: Map returns nullptr, same as every backend.
/// WriteBuffer requires 4-byte-multiple sizes, so shadow and upload sizes round up.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:buffer;

import draconic.core;
import draconic.rhi;
import :api;
import :conversions;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuBuffer final : public Buffer
    {
    public:
        Status Initialize(const WebGpuApi& api, WGPUInstance instance, WGPUDevice device,
                          WGPUQueue queue, const BufferDesc& bufferDesc)
        {
            m_api = &api;
            m_instance = instance;
            m_queue = queue;
            desc = bufferDesc;

            // wgpu does NOT return null for an invalid descriptor - it returns an
            // "invalid" object and raises an uncaptured error. Reject the cases the
            // validator would: a buffer needs a size and at least one usage (GpuToCpu
            // is exempt - its usage is forced to MapRead|CopyDst below).
            if (bufferDesc.size == 0 ||
                (bufferDesc.usage == BufferUsage::None &&
                 bufferDesc.memory != MemoryLocation::GpuToCpu))
            {
                return ErrorCode::InvalidArgument;
            }

            WGPUBufferDescriptor wgpuDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            wgpuDesc.label = ToWgpuStringView(bufferDesc.label);
            wgpuDesc.usage = ToWgpuBufferUsage(bufferDesc.usage, bufferDesc.memory);
            wgpuDesc.size = AlignedSize();
            m_buffer = api.wgpuDeviceCreateBuffer(device, &wgpuDesc);
            if (m_buffer == nullptr)
            {
                return ErrorCode::Unknown;
            }

            if (bufferDesc.memory == MemoryLocation::CpuToGpu ||
                bufferDesc.memory == MemoryLocation::Auto)
            {
                m_shadow.Resize(static_cast<usize>(AlignedSize()));
            }
            return ErrorCode::Ok;
        }

        void* Map() override
        {
            if (!m_shadow.IsEmpty())
            {
                return m_shadow.Data(); // CPU->GPU shadow; Unmap uploads
            }
            if (desc.memory != MemoryLocation::GpuToCpu)
            {
                return nullptr; // GpuOnly has no host view
            }

            // Genuine readback mapping: async map + pump (see :api for why no WaitAny).
            bool done = false;
            bool mapped = false;
            struct Result
            {
                bool* done;
                bool* mapped;
            } result{&done, &mapped};
            WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowProcessEvents;
            callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1,
                                   void*)
            {
                auto* r = static_cast<Result*>(userdata1);
                *r->mapped = status == WGPUMapAsyncStatus_Success;
                *r->done = true;
            };
            callback.userdata1 = &result;
            (void)m_api->wgpuBufferMapAsync(m_buffer, WGPUMapMode_Read, 0,
                                            static_cast<usize>(AlignedSize()), callback);
            m_api->PumpUntil(m_instance, done);
            if (!mapped)
            {
                return nullptr;
            }
            m_readMapped = true;
            // Readback is a read-only view; the RHI contract hands out void* - callers
            // reading through it are fine, writes would be lost (as documented).
            return const_cast<void*>(m_api->wgpuBufferGetConstMappedRange(
                m_buffer, 0, static_cast<usize>(AlignedSize())));
        }

        void Unmap() override
        {
            if (!m_shadow.IsEmpty())
            {
                m_api->wgpuQueueWriteBuffer(m_queue, m_buffer, 0, m_shadow.Data(),
                                            m_shadow.Size());
                return;
            }
            if (m_readMapped)
            {
                m_api->wgpuBufferUnmap(m_buffer);
                m_readMapped = false;
            }
        }

        void Release()
        {
            if (m_buffer != nullptr)
            {
                m_api->wgpuBufferRelease(m_buffer);
                m_buffer = nullptr;
            }
        }

        [[nodiscard]] WGPUBuffer Handle() const { return m_buffer; }

    private:
        [[nodiscard]] u64 AlignedSize() const { return (desc.size + 3ull) & ~3ull; }

        const WebGpuApi* m_api = nullptr;
        WGPUInstance m_instance = nullptr;
        WGPUQueue m_queue = nullptr;
        WGPUBuffer m_buffer = nullptr;
        Array<u8> m_shadow;
        bool m_readMapped = false;
    };
}
