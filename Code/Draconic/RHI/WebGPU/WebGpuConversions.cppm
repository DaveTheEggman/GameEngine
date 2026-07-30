/// draconic.rhi.webgpu:conversions - RHI enum/format -> WebGPU translations.
///
/// The RHI's TextureFormat deliberately follows WebGPU conventions, so the format
/// table is near-1:1. Genuine gaps map to Undefined and the caller fails with
/// NotSupported: RGBA16Unorm/Snorm (native-extension formats, not in webgpu.h's
/// standard enum). AddressMode::ClampToBorder narrows to ClampToEdge (core WebGPU
/// has no border sampling) - visible difference only on shadow-map edge taps.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:conversions;

import draconic.core;
import draconic.rhi;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    [[nodiscard]] inline WGPUTextureFormat ToWgpuTextureFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::Undefined:            return WGPUTextureFormat_Undefined;
        case TextureFormat::R8Unorm:              return WGPUTextureFormat_R8Unorm;
        case TextureFormat::R8Snorm:              return WGPUTextureFormat_R8Snorm;
        case TextureFormat::R8Uint:               return WGPUTextureFormat_R8Uint;
        case TextureFormat::R8Sint:               return WGPUTextureFormat_R8Sint;
        case TextureFormat::R16Uint:              return WGPUTextureFormat_R16Uint;
        case TextureFormat::R16Sint:              return WGPUTextureFormat_R16Sint;
        case TextureFormat::R16Float:             return WGPUTextureFormat_R16Float;
        case TextureFormat::RG8Unorm:             return WGPUTextureFormat_RG8Unorm;
        case TextureFormat::RG8Snorm:             return WGPUTextureFormat_RG8Snorm;
        case TextureFormat::RG8Uint:              return WGPUTextureFormat_RG8Uint;
        case TextureFormat::RG8Sint:              return WGPUTextureFormat_RG8Sint;
        case TextureFormat::R32Uint:              return WGPUTextureFormat_R32Uint;
        case TextureFormat::R32Sint:              return WGPUTextureFormat_R32Sint;
        case TextureFormat::R32Float:             return WGPUTextureFormat_R32Float;
        case TextureFormat::RG16Uint:             return WGPUTextureFormat_RG16Uint;
        case TextureFormat::RG16Sint:             return WGPUTextureFormat_RG16Sint;
        case TextureFormat::RG16Float:            return WGPUTextureFormat_RG16Float;
        case TextureFormat::RGBA8Unorm:           return WGPUTextureFormat_RGBA8Unorm;
        case TextureFormat::RGBA8UnormSrgb:       return WGPUTextureFormat_RGBA8UnormSrgb;
        case TextureFormat::RGBA8Snorm:           return WGPUTextureFormat_RGBA8Snorm;
        case TextureFormat::RGBA8Uint:            return WGPUTextureFormat_RGBA8Uint;
        case TextureFormat::RGBA8Sint:            return WGPUTextureFormat_RGBA8Sint;
        case TextureFormat::BGRA8Unorm:           return WGPUTextureFormat_BGRA8Unorm;
        case TextureFormat::BGRA8UnormSrgb:       return WGPUTextureFormat_BGRA8UnormSrgb;
        case TextureFormat::RGB10A2Unorm:         return WGPUTextureFormat_RGB10A2Unorm;
        case TextureFormat::RGB10A2Uint:          return WGPUTextureFormat_RGB10A2Uint;
        case TextureFormat::RG11B10Float:         return WGPUTextureFormat_RG11B10Ufloat;
        case TextureFormat::RGB9E5Float:          return WGPUTextureFormat_RGB9E5Ufloat;
        case TextureFormat::RG32Uint:             return WGPUTextureFormat_RG32Uint;
        case TextureFormat::RG32Sint:             return WGPUTextureFormat_RG32Sint;
        case TextureFormat::RG32Float:            return WGPUTextureFormat_RG32Float;
        case TextureFormat::RGBA16Uint:           return WGPUTextureFormat_RGBA16Uint;
        case TextureFormat::RGBA16Sint:           return WGPUTextureFormat_RGBA16Sint;
        case TextureFormat::RGBA16Float:          return WGPUTextureFormat_RGBA16Float;
        case TextureFormat::RGBA16Unorm:          return WGPUTextureFormat_Undefined; // native-ext only
        case TextureFormat::RGBA16Snorm:          return WGPUTextureFormat_Undefined; // native-ext only
        case TextureFormat::RGBA32Uint:           return WGPUTextureFormat_RGBA32Uint;
        case TextureFormat::RGBA32Sint:           return WGPUTextureFormat_RGBA32Sint;
        case TextureFormat::RGBA32Float:          return WGPUTextureFormat_RGBA32Float;
        case TextureFormat::Depth16Unorm:         return WGPUTextureFormat_Depth16Unorm;
        case TextureFormat::Depth24Plus:          return WGPUTextureFormat_Depth24Plus;
        case TextureFormat::Depth24PlusStencil8:  return WGPUTextureFormat_Depth24PlusStencil8;
        case TextureFormat::Depth32Float:         return WGPUTextureFormat_Depth32Float;
        case TextureFormat::Depth32FloatStencil8: return WGPUTextureFormat_Depth32FloatStencil8;
        case TextureFormat::Stencil8:             return WGPUTextureFormat_Stencil8;
        case TextureFormat::BC1RGBAUnorm:         return WGPUTextureFormat_BC1RGBAUnorm;
        case TextureFormat::BC1RGBAUnormSrgb:     return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case TextureFormat::BC2RGBAUnorm:         return WGPUTextureFormat_BC2RGBAUnorm;
        case TextureFormat::BC2RGBAUnormSrgb:     return WGPUTextureFormat_BC2RGBAUnormSrgb;
        case TextureFormat::BC3RGBAUnorm:         return WGPUTextureFormat_BC3RGBAUnorm;
        case TextureFormat::BC3RGBAUnormSrgb:     return WGPUTextureFormat_BC3RGBAUnormSrgb;
        case TextureFormat::BC4RUnorm:            return WGPUTextureFormat_BC4RUnorm;
        case TextureFormat::BC4RSnorm:            return WGPUTextureFormat_BC4RSnorm;
        case TextureFormat::BC5RGUnorm:           return WGPUTextureFormat_BC5RGUnorm;
        case TextureFormat::BC5RGSnorm:           return WGPUTextureFormat_BC5RGSnorm;
        case TextureFormat::BC6HRGBUfloat:        return WGPUTextureFormat_BC6HRGBUfloat;
        case TextureFormat::BC6HRGBFloat:         return WGPUTextureFormat_BC6HRGBFloat;
        case TextureFormat::BC7RGBAUnorm:         return WGPUTextureFormat_BC7RGBAUnorm;
        case TextureFormat::BC7RGBAUnormSrgb:     return WGPUTextureFormat_BC7RGBAUnormSrgb;
        default:                                  return WGPUTextureFormat_Undefined;
        }
    }

    [[nodiscard]] inline WGPUBufferUsage ToWgpuBufferUsage(BufferUsage usage,
                                                           MemoryLocation memory)
    {
        WGPUBufferUsage out = WGPUBufferUsage_None;
        if (HasFlag(usage, BufferUsage::CopySrc))
        {
            out |= WGPUBufferUsage_CopySrc;
        }
        if (HasFlag(usage, BufferUsage::CopyDst))
        {
            out |= WGPUBufferUsage_CopyDst;
        }
        if (HasFlag(usage, BufferUsage::Vertex))
        {
            out |= WGPUBufferUsage_Vertex;
        }
        if (HasFlag(usage, BufferUsage::Index))
        {
            out |= WGPUBufferUsage_Index;
        }
        if (HasFlag(usage, BufferUsage::Uniform))
        {
            out |= WGPUBufferUsage_Uniform;
        }
        if (HasFlag(usage, BufferUsage::Storage) || HasFlag(usage, BufferUsage::StorageRead))
        {
            out |= WGPUBufferUsage_Storage;
        }
        if (HasFlag(usage, BufferUsage::Indirect))
        {
            out |= WGPUBufferUsage_Indirect;
        }

        // The Map emulation's transport (see :buffer): CPU->GPU shadows upload through
        // WriteBuffer (CopyDst); GPU->CPU readback maps for real, which WebGPU only
        // validates as MapRead|CopyDst - nothing else may be combined with MapRead.
        if (memory == MemoryLocation::CpuToGpu)
        {
            out |= WGPUBufferUsage_CopyDst;
        }
        else if (memory == MemoryLocation::GpuToCpu)
        {
            out = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        }
        return out;
    }

    [[nodiscard]] inline WGPUTextureUsage ToWgpuTextureUsage(TextureUsage usage)
    {
        const auto HasFlag = [usage](TextureUsage flag)
        { return (static_cast<u32>(usage) & static_cast<u32>(flag)) != 0; };
        WGPUTextureUsage out = WGPUTextureUsage_None;
        if (HasFlag(TextureUsage::CopySrc))
        {
            out |= WGPUTextureUsage_CopySrc;
        }
        if (HasFlag(TextureUsage::CopyDst))
        {
            out |= WGPUTextureUsage_CopyDst;
        }
        if (HasFlag(TextureUsage::Sampled))
        {
            out |= WGPUTextureUsage_TextureBinding;
        }
        if (HasFlag(TextureUsage::Storage))
        {
            out |= WGPUTextureUsage_StorageBinding;
        }
        if (HasFlag(TextureUsage::RenderTarget) ||
            HasFlag(TextureUsage::DepthStencil))
        {
            out |= WGPUTextureUsage_RenderAttachment;
        }
        // InputAttachment is a Vulkan concept; sampling covers its WebGPU shape.
        if (HasFlag(TextureUsage::InputAttachment))
        {
            out |= WGPUTextureUsage_TextureBinding;
        }
        return out;
    }

    [[nodiscard]] inline WGPUTextureDimension ToWgpuTextureDimension(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::Texture1D:
            return WGPUTextureDimension_1D;
        case TextureDimension::Texture2D:
            return WGPUTextureDimension_2D;
        case TextureDimension::Texture3D:
            return WGPUTextureDimension_3D;
        }
        return WGPUTextureDimension_2D;
    }

    [[nodiscard]] inline WGPUTextureViewDimension
    ToWgpuTextureViewDimension(TextureViewDimension dimension)
    {
        switch (dimension)
        {
        case TextureViewDimension::Texture1D:
            return WGPUTextureViewDimension_1D;
        case TextureViewDimension::Texture1DArray:
            return WGPUTextureViewDimension_Undefined; // no 1D arrays in WebGPU
        case TextureViewDimension::Texture2D:
            return WGPUTextureViewDimension_2D;
        case TextureViewDimension::Texture2DArray:
            return WGPUTextureViewDimension_2DArray;
        case TextureViewDimension::TextureCube:
            return WGPUTextureViewDimension_Cube;
        case TextureViewDimension::TextureCubeArray:
            return WGPUTextureViewDimension_CubeArray;
        case TextureViewDimension::Texture3D:
            return WGPUTextureViewDimension_3D;
        }
        return WGPUTextureViewDimension_2D;
    }

    [[nodiscard]] inline WGPUTextureAspect ToWgpuTextureAspect(TextureAspect aspect)
    {
        switch (aspect)
        {
        case TextureAspect::All:
            return WGPUTextureAspect_All;
        case TextureAspect::DepthOnly:
            return WGPUTextureAspect_DepthOnly;
        case TextureAspect::StencilOnly:
            return WGPUTextureAspect_StencilOnly;
        }
        return WGPUTextureAspect_All;
    }

    [[nodiscard]] inline WGPUFilterMode ToWgpuFilterMode(FilterMode filter)
    {
        return filter == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    }

    [[nodiscard]] inline WGPUMipmapFilterMode ToWgpuMipmapFilterMode(MipmapFilterMode filter)
    {
        return filter == MipmapFilterMode::Nearest ? WGPUMipmapFilterMode_Nearest
                                                   : WGPUMipmapFilterMode_Linear;
    }

    [[nodiscard]] inline WGPUAddressMode ToWgpuAddressMode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::Repeat:
            return WGPUAddressMode_Repeat;
        case AddressMode::MirrorRepeat:
            return WGPUAddressMode_MirrorRepeat;
        case AddressMode::ClampToEdge:
            return WGPUAddressMode_ClampToEdge;
        case AddressMode::ClampToBorder:
            // Core WebGPU has no border sampling; edge clamp is the nearest behavior.
            return WGPUAddressMode_ClampToEdge;
        }
        return WGPUAddressMode_Repeat;
    }

    [[nodiscard]] inline WGPUCompareFunction ToWgpuCompareFunction(CompareFunction function)
    {
        switch (function)
        {
        case CompareFunction::Never:
            return WGPUCompareFunction_Never;
        case CompareFunction::Less:
            return WGPUCompareFunction_Less;
        case CompareFunction::Equal:
            return WGPUCompareFunction_Equal;
        case CompareFunction::LessEqual:
            return WGPUCompareFunction_LessEqual;
        case CompareFunction::Greater:
            return WGPUCompareFunction_Greater;
        case CompareFunction::NotEqual:
            return WGPUCompareFunction_NotEqual;
        case CompareFunction::GreaterEqual:
            return WGPUCompareFunction_GreaterEqual;
        case CompareFunction::Always:
            return WGPUCompareFunction_Always;
        }
        return WGPUCompareFunction_Always;
    }

    /// Label helper: RHI labels are UTF-8 StringViews, WebGPU wants {data,length}.
    [[nodiscard]] inline WGPUStringView ToWgpuStringView(StringView label)
    {
        return WGPUStringView{reinterpret_cast<const char*>(label.Data()), label.Size()};
    }
}
