// Draconic::Texture — :format_utils partition
//
// Maps an image PixelFormat to the RHI TextureFormat, honoring the source
// data's color space. Ported from Sedulous.Textures/TextureFormatUtils.bf.

module;
#include "Core/Prelude.h"

export module draconic.texture:format_utils;

import draconic.core;
import draconic.rhi;
import draconic.image;

using namespace draconic::core;

export namespace draconic::texture
{
    namespace rhi = draconic::rhi;
    namespace img = draconic::image;

    class TextureFormatUtils
    {
    public:
        // Image PixelFormat -> RHI TextureFormat. When the data is sRGB and the
        // format has an sRGB variant (8-bit RGB/RGBA/BGR/BGRA), the sRGB GPU
        // format is returned so hardware decodes sRGB->linear on sample. Float
        // and 1/2-channel formats pass through. 3-channel maps to RGBA (GPUs
        // don't support 3-channel).
        [[nodiscard]] static rhi::TextureFormat Convert(img::PixelFormat format, img::ImageColorSpace colorSpace)
        {
            if (colorSpace == img::ImageColorSpace::Srgb)
            {
                switch (format)
                {
                    case img::PixelFormat::RGB8:
                    case img::PixelFormat::RGBA8: return rhi::TextureFormat::RGBA8UnormSrgb;
                    case img::PixelFormat::BGR8:
                    case img::PixelFormat::BGRA8: return rhi::TextureFormat::BGRA8UnormSrgb;
                    default: break;
                }
            }

            switch (format)
            {
                case img::PixelFormat::R8:      return rhi::TextureFormat::R8Unorm;
                case img::PixelFormat::RG8:     return rhi::TextureFormat::RG8Unorm;
                case img::PixelFormat::RGB8:    return rhi::TextureFormat::RGBA8Unorm;
                case img::PixelFormat::RGBA8:   return rhi::TextureFormat::RGBA8Unorm;
                case img::PixelFormat::BGR8:    return rhi::TextureFormat::BGRA8Unorm;
                case img::PixelFormat::BGRA8:   return rhi::TextureFormat::BGRA8Unorm;
                case img::PixelFormat::R16F:    return rhi::TextureFormat::R16Float;
                case img::PixelFormat::RG16F:   return rhi::TextureFormat::RG16Float;
                case img::PixelFormat::RGB16F:  return rhi::TextureFormat::RGBA16Float;
                case img::PixelFormat::RGBA16F: return rhi::TextureFormat::RGBA16Float;
                case img::PixelFormat::R32F:    return rhi::TextureFormat::R32Float;
                case img::PixelFormat::RG32F:   return rhi::TextureFormat::RG32Float;
                case img::PixelFormat::RGB32F:  return rhi::TextureFormat::RGBA32Float;
                case img::PixelFormat::RGBA32F: return rhi::TextureFormat::RGBA32Float;
                default:                        return rhi::TextureFormat::RGBA8Unorm;
            }
        }
    };
}
