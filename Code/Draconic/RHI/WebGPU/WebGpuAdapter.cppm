/// draconic.rhi.webgpu:adapter - Adapter over WGPUAdapter.
///
/// GetInfo maps WGPUAdapterInfo/limits/features onto the RHI's AdapterInfo;
/// CreateDevice performs the async wgpuAdapterRequestDevice through the
/// ProcessEvents pump (see :api) and registers the device-lost + uncaptured-error
/// callbacks before the WebGpuDevice wrapper exists to receive them.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:adapter;

import draconic.core;
import draconic.rhi;
import :api;
import :device;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuAdapter final : public Adapter
    {
    public:
        WebGpuAdapter(const WebGpuApi& api, WGPUInstance instance, WGPUAdapter adapter,
                      IAllocator& allocator)
            : m_api(&api), m_instance(instance), m_adapter(adapter), m_allocator(allocator)
        {
        }

        [[nodiscard]] WGPUAdapter Handle() const { return m_adapter; }

        void GetInfo(AdapterInfo& out) override
        {
            WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
            m_api->wgpuAdapterGetInfo(m_adapter, &info);

            out.name = String(StringView(reinterpret_cast<const utf8char*>(info.device.data),
                                         info.device.length));
            out.vendorId = info.vendorID;
            out.deviceId = info.deviceID;
            switch (info.adapterType)
            {
            case WGPUAdapterType_DiscreteGPU:
                out.type = AdapterType::DiscreteGpu;
                break;
            case WGPUAdapterType_IntegratedGPU:
                out.type = AdapterType::IntegratedGpu;
                break;
            case WGPUAdapterType_CPU:
                out.type = AdapterType::Cpu;
                break;
            default:
                out.type = AdapterType::Unknown;
                break;
            }
            m_api->wgpuAdapterInfoFreeMembers(info);

            WGPULimits limits = WGPU_LIMITS_INIT;
            if (m_api->wgpuAdapterGetLimits(m_adapter, &limits) == WGPUStatus_Success)
            {
                out.supportedFeatures.maxBindGroups = limits.maxBindGroups;
                out.supportedFeatures.maxBindingsPerGroup =
                    limits.maxSampledTexturesPerShaderStage;
                out.supportedFeatures.maxTextureDimension2D = limits.maxTextureDimension2D;
                out.supportedFeatures.maxTextureArrayLayers = limits.maxTextureArrayLayers;
                out.supportedFeatures.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
                out.supportedFeatures.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
                out.supportedFeatures.maxComputeWorkgroupSizeZ = limits.maxComputeWorkgroupSizeZ;
                out.supportedFeatures.maxComputeWorkgroupsPerDimension =
                    limits.maxComputeWorkgroupsPerDimension;
                out.supportedFeatures.minUniformBufferOffsetAlignment =
                    limits.minUniformBufferOffsetAlignment;
                out.supportedFeatures.minStorageBufferOffsetAlignment =
                    limits.minStorageBufferOffsetAlignment;
                out.supportedFeatures.maxBufferSize = limits.maxBufferSize;
            }

            WGPUSupportedFeatures features = {};
            m_api->wgpuAdapterGetFeatures(m_adapter, &features);
            for (usize i = 0; i < features.featureCount; ++i)
            {
                switch (features.features[i])
                {
                case WGPUFeatureName_TimestampQuery:
                    out.supportedFeatures.timestampQueries = true;
                    break;
                case WGPUFeatureName_TextureCompressionBC:
                    out.supportedFeatures.textureCompressionBC = true;
                    break;
                case WGPUFeatureName_TextureCompressionASTC:
                    out.supportedFeatures.textureCompressionASTC = true;
                    break;
                case WGPUFeatureName_DepthClipControl:
                    out.supportedFeatures.depthClamp = true;
                    break;
                default:
                    break;
                }
            }
            m_api->wgpuSupportedFeaturesFreeMembers(features);

            // WebGPU guarantees per-attachment blend state; push constants are an
            // EMULATION (dynamic-offset uniform ring) so the declared budget is what
            // the emulation will honor.
            out.supportedFeatures.independentBlend = true;
        }

        Status CreateDevice(const DeviceDesc& desc, Device*& out) override
        {
            out = nullptr;

            // The wrapper does not exist until the request completes, so loss routes
            // through a slot the callback fills lazily.
            struct LostRoute
            {
                WebGpuDevice* device = nullptr;
            };
            // TODO(webgpu): one pointer-sized intentional leak per device - the lost
            // callback can outlive every safe free point we control. Fold into the
            // wrapper once teardown ordering is settled.
            auto* lostRoute = m_allocator.New<LostRoute>();

            Array<WGPUFeatureName> required;
            AdapterInfo info;
            GetInfo(info);
            if (info.supportedFeatures.timestampQueries &&
                desc.requiredFeatures.timestampQueries)
            {
                required.PushBack(WGPUFeatureName_TimestampQuery);
            }
            if (info.supportedFeatures.textureCompressionBC)
            {
                required.PushBack(WGPUFeatureName_TextureCompressionBC);
            }
            if (info.supportedFeatures.depthClamp)
            {
                required.PushBack(WGPUFeatureName_DepthClipControl);
            }

            WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
            deviceDesc.requiredFeatureCount = required.Size();
            deviceDesc.requiredFeatures = required.Data();
            deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
            deviceDesc.deviceLostCallbackInfo.callback =
                [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message,
                   void* userdata1, void*)
            {
                if (reason == WGPUDeviceLostReason_Destroyed)
                {
                    return; // orderly teardown is not a loss
                }
                auto* route = static_cast<LostRoute*>(userdata1);
                if (route->device != nullptr)
                {
                    route->device->MarkLost();
                }
                LogErrorf("[webgpu] device LOST (reason %d): %.*s", static_cast<int>(reason),
                          static_cast<int>(message.length),
                          reinterpret_cast<const char*>(message.data));
            };
            deviceDesc.deviceLostCallbackInfo.userdata1 = lostRoute;
            deviceDesc.uncapturedErrorCallbackInfo.callback =
                [](WGPUDevice const*, WGPUErrorType errorType, WGPUStringView message, void*,
                   void*)
            {
                LogErrorf("[webgpu] uncaptured error (type %d): %.*s",
                          static_cast<int>(errorType), static_cast<int>(message.length),
                          reinterpret_cast<const char*>(message.data));
            };

            WGPUDevice device = nullptr;
            bool done = false;
            struct Result
            {
                WGPUDevice* device;
                bool* done;
            } result{&device, &done};

            WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowProcessEvents;
            callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice created,
                                   WGPUStringView message, void* userdata1, void*)
            {
                auto* r = static_cast<Result*>(userdata1);
                if (status == WGPURequestDeviceStatus_Success)
                {
                    *r->device = created;
                }
                else
                {
                    LogErrorf("[webgpu] RequestDevice failed: %.*s",
                              static_cast<int>(message.length),
                              reinterpret_cast<const char*>(message.data));
                }
                *r->done = true;
            };
            callback.userdata1 = &result;

            (void)m_api->wgpuAdapterRequestDevice(m_adapter, &deviceDesc, callback);
            m_api->PumpUntil(m_instance, done);

            if (device == nullptr)
            {
                m_allocator.Delete(lostRoute);
                return ErrorCode::Unknown;
            }

            auto* wrapper = m_allocator.New<WebGpuDevice>(*m_api, m_instance, device,
                                                          m_allocator);
            wrapper->features = info.supportedFeatures;
            lostRoute->device = wrapper;
            out = wrapper;
            return ErrorCode::Ok;
        }

    private:
        const WebGpuApi* m_api;
        WGPUInstance m_instance;
        WGPUAdapter m_adapter;
        IAllocator& m_allocator;
    };
}
