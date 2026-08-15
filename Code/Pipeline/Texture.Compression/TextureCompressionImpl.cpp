// texture.compression impl - the encoder dispatch + policy table. The heavy encoder headers live
// ONLY here (GCC module hygiene). bc7enc = BC7; rgbcx = BC1/3/4/5.

module;
#include "Core/Prelude.h"
#include "bc7enc.h"
#include "rgbcx.h"

module texture.compression;

import foundation.core;
import foundation.rhi;

using namespace foundation::core;

namespace texcomp
{
    namespace rhi = foundation::rhi;

    // The encoders need a one-time global table init before first use.
    static void EnsureInit()
    {
        static const bool once = []()
        {
            bc7enc_compress_block_init();
            rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
            return true;
        }();
        (void)once;
    }

    // Gather the 4x4 RGBA block at block coord (bx, by), clamping source coords so edge/NPOT/small
    // levels pad with their border texels (the encoder handles sub-block levels this way).
    static void GatherBlock(const u8* rgba, u32 w, u32 h, u32 bx, u32 by, u8 out[64]) noexcept
    {
        for (u32 py = 0; py < 4; ++py)
        {
            const u32 sy = Min(by * 4 + py, h - 1);
            for (u32 px = 0; px < 4; ++px)
            {
                const u32 sx = Min(bx * 4 + px, w - 1);
                const u8* src = rgba + (static_cast<usize>(sy) * w + sx) * 4;
                u8* dst = out + (static_cast<usize>(py) * 4 + px) * 4;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
            }
        }
    }

    usize BlockCompressedSize(rhi::TextureFormat format, u32 width, u32 height) noexcept
    {
        return rhi::CompressedLevelBytes(format, width, height);
    }

    rhi::TextureFormat ResolveCompressedFormat(TextureUsage usage, bool sRGB, bool hasAlpha,
                                               CompressionChoice choice, u32 width, u32 height,
                                               const TargetProfile& profile,
                                               rhi::TextureFormat uncompressed) noexcept
    {
        using F = rhi::TextureFormat;
        // Escape hatches (Decision 5, last row): authored None, small/UI/pixel-art, HDR (no BC6H yet).
        if (choice == CompressionChoice::None)
        {
            return uncompressed;
        }
        if (width <= 64 && height <= 64)
        {
            return uncompressed;
        }
        if (usage == TextureUsage::HDR)
        {
            return uncompressed; // BC6H not vendored in P1
        }

        if (profile.bc)
        {
            switch (usage)
            {
            case TextureUsage::Color:
                if (hasAlpha || choice == CompressionChoice::Quality)
                {
                    return sRGB ? F::BC7RGBAUnormSrgb : F::BC7RGBAUnorm;
                }
                return sRGB ? F::BC1RGBAUnormSrgb : F::BC1RGBAUnorm; // opaque, Default
            case TextureUsage::Normal:
                return F::BC5RGUnorm; // tangent normal, linear RG
            case TextureUsage::Mask:
                return F::BC4RUnorm; // single channel
            case TextureUsage::HDR:
                return uncompressed; // handled above
            }
        }
        // ASTC/ETC2 profiles land in P3; for now anything unsupported stays uncompressed.
        return uncompressed;
    }

    Array<byte> EncodeBlockCompressed(const u8* rgba, u32 width, u32 height, rhi::TextureFormat format,
                                      u8 quality)
    {
        Array<byte> out;
        if (rgba == nullptr || width == 0 || height == 0 || !rhi::IsCompressed(format))
        {
            return out;
        }
        EnsureInit();

        const u32 bx = (width + 3) / 4;
        const u32 by = (height + 3) / 4;
        out.Resize(BlockCompressedSize(format, width, height));

        // rgbcx level 0..18 (BC1/3); bc7enc uber level 0..4. Map both from `quality` 0..255.
        const u32 rgbcxLevel = (static_cast<u32>(quality) * rgbcx::MAX_LEVEL) / 255u;
        bc7enc_compress_block_params bc7params;
        bc7enc_compress_block_params_init(&bc7params);
        bc7params.m_uber_level = Min<u32>(4u, static_cast<u32>(quality) / 51u); // 0..4

        u8 block[64];
        usize off = 0;
        for (u32 y = 0; y < by; ++y)
        {
            for (u32 x = 0; x < bx; ++x)
            {
                GatherBlock(rgba, width, height, x, y, block);
                void* dst = out.Data() + off;
                switch (format)
                {
                case rhi::TextureFormat::BC1RGBAUnorm:
                case rhi::TextureFormat::BC1RGBAUnormSrgb:
                    rgbcx::encode_bc1(rgbcxLevel, dst, block, /*allow_3color*/ true,
                                      /*use_transparent_texels_for_black*/ false);
                    off += 8;
                    break;
                case rhi::TextureFormat::BC3RGBAUnorm:
                case rhi::TextureFormat::BC3RGBAUnormSrgb:
                    rgbcx::encode_bc3(rgbcxLevel, dst, block);
                    off += 16;
                    break;
                case rhi::TextureFormat::BC4RUnorm:
                case rhi::TextureFormat::BC4RSnorm:
                    rgbcx::encode_bc4(dst, block, /*stride*/ 4);
                    off += 8;
                    break;
                case rhi::TextureFormat::BC5RGUnorm:
                case rhi::TextureFormat::BC5RGSnorm:
                    rgbcx::encode_bc5(dst, block, /*chan0*/ 0, /*chan1*/ 1, /*stride*/ 4);
                    off += 16;
                    break;
                case rhi::TextureFormat::BC7RGBAUnorm:
                case rhi::TextureFormat::BC7RGBAUnormSrgb:
                    bc7enc_compress_block(dst, block, &bc7params);
                    off += 16;
                    break;
                default:
                    return Array<byte>{}; // BC6H / ASTC not supported in this build
                }
            }
        }
        return out;
    }
}
