// texture.compression tests - the policy table (pure logic), exact block sizes, and a real
// encode->decode round-trip whose PSNR proves the encoders actually run and reconstruct. Decoding
// uses the vendored bc7enc/rgbcx decoders directly (test-only; the module itself only encodes).

#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "rgbcx.h"
#include "bc7decomp.h"
#include "astcenc.h"
#include <cmath>

import foundation.core;
import foundation.rhi;
import texture.compression;

using namespace foundation::core;
namespace rhi = foundation::rhi;
using namespace texcomp;

namespace
{
    // A deterministic width*height RGBA8 test image: smooth channel ramps (compressible) with a
    // touch of block-local variation so the encoder has real work to do.
    Array<u8> MakeImage(u32 w, u32 h, bool alpha)
    {
        Array<u8> px;
        px.Resize(static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y)
        {
            for (u32 x = 0; x < w; ++x)
            {
                u8* p = px.Data() + (static_cast<usize>(y) * w + x) * 4;
                p[0] = static_cast<u8>((x * 255) / (w - 1));
                p[1] = static_cast<u8>((y * 255) / (h - 1));
                p[2] = static_cast<u8>(((x + y) * 255) / (w + h - 2));
                p[3] = alpha ? static_cast<u8>((x * 255) / (w - 1)) : 255;
            }
        }
        return px;
    }

    // PSNR (dB) between two equal-size RGBA8 buffers over the given channel count.
    double Psnr(const u8* a, const u8* b, usize pixels, u32 channels)
    {
        double sse = 0.0;
        for (usize i = 0; i < pixels; ++i)
        {
            for (u32 c = 0; c < channels; ++c)
            {
                const double d = static_cast<double>(a[i * 4 + c]) - static_cast<double>(b[i * 4 + c]);
                sse += d * d;
            }
        }
        if (sse <= 0.0)
        {
            return 99.0;
        }
        const double mse = sse / (static_cast<double>(pixels) * channels);
        return 10.0 * std::log10((255.0 * 255.0) / mse);
    }

    // Decode BC1/BC7 back to a tightly-packed RGBA8 image so we can measure reconstruction error.
    Array<u8> DecodeBc(const Array<byte>& blocks, u32 w, u32 h, rhi::TextureFormat fmt)
    {
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        Array<u8> out;
        out.Resize(static_cast<usize>(w) * h * 4);
        const u32 bx = (w + 3) / 4;
        const u32 by = (h + 3) / 4;
        const usize blockBytes = (fmt == rhi::TextureFormat::BC1RGBAUnorm) ? 8u : 16u;
        const byte* src = blocks.Data();
        for (u32 gy = 0; gy < by; ++gy)
        {
            for (u32 gx = 0; gx < bx; ++gx)
            {
                u8 tile[64];
                const void* blk = src + (static_cast<usize>(gy) * bx + gx) * blockBytes;
                if (fmt == rhi::TextureFormat::BC1RGBAUnorm)
                {
                    rgbcx::unpack_bc1(blk, tile, true, rgbcx::bc1_approx_mode::cBC1Ideal);
                }
                else
                {
                    bc7decomp::unpack_bc7(blk, reinterpret_cast<bc7decomp::color_rgba*>(tile));
                }
                for (u32 py = 0; py < 4; ++py)
                {
                    const u32 sy = gy * 4 + py;
                    if (sy >= h) { break; }
                    for (u32 px = 0; px < 4; ++px)
                    {
                        const u32 sx = gx * 4 + px;
                        if (sx >= w) { break; }
                        u8* d = out.Data() + (static_cast<usize>(sy) * w + sx) * 4;
                        const u8* s = tile + (static_cast<usize>(py) * 4 + px) * 4;
                        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    }
                }
            }
        }
        return out;
    }

    // Decode ASTC 4x4 back to RGBA8 via astcenc (test-only; the module only encodes).
    Array<u8> DecodeAstc(const Array<byte>& blocks, u32 w, u32 h, bool srgb)
    {
        Array<u8> out;
        astcenc_config config{};
        const astcenc_profile profile = srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;
        if (astcenc_config_init(profile, 4, 4, 1, ASTCENC_PRE_MEDIUM, 0, &config) != ASTCENC_SUCCESS)
        {
            return out;
        }
        astcenc_context* ctx = nullptr;
        if (astcenc_context_alloc(&config, 1, &ctx, nullptr) != ASTCENC_SUCCESS)
        {
            return out;
        }
        out.Resize(static_cast<usize>(w) * h * 4);
        void* slice = out.Data();
        astcenc_image image{};
        image.dim_x = w;
        image.dim_y = h;
        image.dim_z = 1;
        image.data_type = ASTCENC_TYPE_U8;
        image.data = &slice;
        const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
        const astcenc_error err = astcenc_decompress_image(
            ctx, reinterpret_cast<const uint8_t*>(blocks.Data()), blocks.Size(), &image, &swizzle, 0);
        astcenc_context_free(ctx);
        if (err != ASTCENC_SUCCESS)
        {
            out.Clear();
        }
        return out;
    }
}

TEST_CASE("ResolveCompressedFormat - Decision 5 policy table (BC profile)")
{
    const TargetProfile bc = DesktopProfile();
    const auto uncompressed = rhi::TextureFormat::RGBA8Unorm;

    // Color: opaque Default -> BC1; alpha -> BC7; Quality forces BC7 even opaque; sRGB variants.
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, false, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC1RGBAUnorm);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, false, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC1RGBAUnormSrgb);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, true, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC7RGBAUnorm);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, true, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC7RGBAUnormSrgb);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, false, CompressionChoice::Quality, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC7RGBAUnorm);

    // Normal -> BC5, Mask -> BC4 (both linear, ignore sRGB).
    CHECK(ResolveCompressedFormat(TextureUsage::Normal, false, false, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC5RGUnorm);
    CHECK(ResolveCompressedFormat(TextureUsage::Mask, false, false, CompressionChoice::Default, 256, 256, bc, uncompressed) == rhi::TextureFormat::BC4RUnorm);

    // Escape hatches -> uncompressed: authored None, small (<=64px), HDR (no BC6H yet).
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, true, CompressionChoice::None, 256, 256, bc, uncompressed) == uncompressed);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, false, CompressionChoice::Default, 64, 64, bc, uncompressed) == uncompressed);
    CHECK(ResolveCompressedFormat(TextureUsage::HDR, false, false, CompressionChoice::Default, 256, 256, bc, uncompressed) == uncompressed);

    // No supported family -> uncompressed regardless of usage.
    const TargetProfile none{false, false, false};
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, true, CompressionChoice::Default, 256, 256, none, uncompressed) == uncompressed);
}

TEST_CASE("BlockCompressedSize - exact 4x4 block-ceil bytes")
{
    // 256x256 = 64x64 blocks = 4096 blocks. BC1/BC4 = 8B/block, BC7/BC5/BC3 = 16B/block.
    CHECK(BlockCompressedSize(rhi::TextureFormat::BC1RGBAUnorm, 256, 256) == 4096u * 8u);
    CHECK(BlockCompressedSize(rhi::TextureFormat::BC4RUnorm, 256, 256) == 4096u * 8u);
    CHECK(BlockCompressedSize(rhi::TextureFormat::BC7RGBAUnorm, 256, 256) == 4096u * 16u);
    CHECK(BlockCompressedSize(rhi::TextureFormat::BC5RGUnorm, 256, 256) == 4096u * 16u);
    // NPOT / sub-block rounds UP: 65x65 -> 17x17 blocks.
    CHECK(BlockCompressedSize(rhi::TextureFormat::BC7RGBAUnorm, 65, 65) == 17u * 17u * 16u);
}

TEST_CASE("EncodeBlockCompressed - BC1 round-trip PSNR + exact size")
{
    const u32 w = 128, h = 128;
    const Array<u8> img = MakeImage(w, h, /*alpha*/ false);
    const Array<byte> enc = EncodeBlockCompressed(img.Data(), w, h, rhi::TextureFormat::BC1RGBAUnorm, 200);

    REQUIRE(enc.Size() == BlockCompressedSize(rhi::TextureFormat::BC1RGBAUnorm, w, h));
    const Array<u8> dec = DecodeBc(enc, w, h, rhi::TextureFormat::BC1RGBAUnorm);
    const double psnr = Psnr(img.Data(), dec.Data(), static_cast<usize>(w) * h, 3);
    // BC1 on a smooth ramp reconstructs well above this floor; a broken encoder scores far lower.
    CHECK(psnr > 30.0);
}

TEST_CASE("EncodeBlockCompressed - BC7 round-trip PSNR (RGBA) beats BC1")
{
    const u32 w = 128, h = 128;
    const Array<u8> img = MakeImage(w, h, /*alpha*/ true);

    const Array<byte> bc7 = EncodeBlockCompressed(img.Data(), w, h, rhi::TextureFormat::BC7RGBAUnorm, 255);
    REQUIRE(bc7.Size() == BlockCompressedSize(rhi::TextureFormat::BC7RGBAUnorm, w, h));
    const Array<u8> dec7 = DecodeBc(bc7, w, h, rhi::TextureFormat::BC7RGBAUnorm);
    const double psnr7 = Psnr(img.Data(), dec7.Data(), static_cast<usize>(w) * h, 4);
    // BC7 is near-lossless on a smooth gradient; high floor guards the full 4-channel path.
    CHECK(psnr7 > 40.0);
}

TEST_CASE("ResolveCompressedFormat - ASTC (mobile) profile")
{
    const TargetProfile astc = MobileProfile();
    const auto uncompressed = rhi::TextureFormat::RGBA8Unorm;

    // One 4x4 format covers every LDR usage; sRGB only applies to color.
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, true, CompressionChoice::Default, 256, 256, astc, uncompressed) == rhi::TextureFormat::ASTC4x4UnormSrgb);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, false, CompressionChoice::Default, 256, 256, astc, uncompressed) == rhi::TextureFormat::ASTC4x4Unorm);
    CHECK(ResolveCompressedFormat(TextureUsage::Normal, false, false, CompressionChoice::Default, 256, 256, astc, uncompressed) == rhi::TextureFormat::ASTC4x4Unorm);
    CHECK(ResolveCompressedFormat(TextureUsage::Mask, true, false, CompressionChoice::Default, 256, 256, astc, uncompressed) == rhi::TextureFormat::ASTC4x4Unorm); // linear map ignores sRGB

    // Same escape hatches as BC: None, small, HDR.
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, false, CompressionChoice::None, 256, 256, astc, uncompressed) == uncompressed);
    CHECK(ResolveCompressedFormat(TextureUsage::Color, true, false, CompressionChoice::Default, 64, 64, astc, uncompressed) == uncompressed);
    CHECK(ResolveCompressedFormat(TextureUsage::HDR, false, false, CompressionChoice::Default, 256, 256, astc, uncompressed) == uncompressed);

    // A profile that supports BOTH families prefers BC (desktop-first).
    const TargetProfile both{true, true, false};
    CHECK(ResolveCompressedFormat(TextureUsage::Color, false, false, CompressionChoice::Default, 256, 256, both, uncompressed) == rhi::TextureFormat::BC1RGBAUnorm);
}

TEST_CASE("EncodeBlockCompressed - ASTC 4x4 round-trip PSNR + exact size")
{
    const u32 w = 128, h = 128;
    const Array<u8> img = MakeImage(w, h, /*alpha*/ true);

    const Array<byte> enc =
        EncodeBlockCompressed(img.Data(), w, h, rhi::TextureFormat::ASTC4x4Unorm, 200);
    REQUIRE(enc.Size() == BlockCompressedSize(rhi::TextureFormat::ASTC4x4Unorm, w, h));
    // ASTC 4x4 = 16 bytes per 4x4 block = 8 bpp, same footprint as BC7.
    CHECK(enc.Size() == static_cast<usize>((w / 4) * (h / 4)) * 16u);

    const Array<u8> dec = DecodeAstc(enc, w, h, /*srgb*/ false);
    REQUIRE(dec.Size() == static_cast<usize>(w) * h * 4);
    const double psnr = Psnr(img.Data(), dec.Data(), static_cast<usize>(w) * h, 4);
    // ASTC 4x4 is near-lossless on a smooth gradient; a broken encode scores far lower.
    CHECK(psnr > 40.0);
}

TEST_CASE("EncodeBlockCompressed - rejects non-BC formats + null input")
{
    const u32 w = 8, h = 8;
    const Array<u8> img = MakeImage(w, h, false);
    CHECK(EncodeBlockCompressed(img.Data(), w, h, rhi::TextureFormat::RGBA8Unorm, 128).Size() == 0);
    CHECK(EncodeBlockCompressed(nullptr, w, h, rhi::TextureFormat::BC1RGBAUnorm, 128).Size() == 0);
}
