// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Texture.Compression - `texture.compression`.
//
// The ONLY code that includes the block-compression encoder headers (bc7enc/rgbcx). Two jobs:
//   - ResolveCompressedFormat: the format POLICY TABLE - given a source's authored semantics and the
//     export target's capabilities, pick the cooked rhi::TextureFormat (or an uncompressed fallback).
//   - EncodeBlockCompressed: encode one mip level of RGBA8 to a BC format's block bytes.
// Cook/import-time only, UI-free (the Pipeline rule); the heavy encoder headers stay in the impl unit
// (GCC module hygiene). BC and ASTC are implemented.
// TODO: BC6H/HDR encoding is not implemented.

module;
#include "Core/Prelude.h"

export module texture.compression;

import foundation.core;
import foundation.rhi;

using namespace foundation::core;

export namespace texcomp
{
    namespace rhi = foundation::rhi;

    // What a source texture is FOR - the SEMANTIC hint the policy table keys on. Authored on the asset
    // (never the runtime struct); model import sets it from the material slot, standalone defaults Color.
    enum class TextureUsage : u8
    {
        Color,  // albedo / UI - sRGB (or linear) color
        Normal, // tangent-space normal map (linear RG)
        Mask,   // single-channel mask / height / roughness
        HDR,    // RGBE / half-float radiance (linear) - BC6H unsigned on BC targets
    };

    // Authored compression choice on the asset.
    enum class CompressionChoice : u8
    {
        Default, // the policy table below
        None,    // force uncompressed (today's raw path - the escape hatch)
        Quality, // force the high-quality format (BC7) + high encoder effort
    };

    // The compressed-texture FAMILIES the export target's devices support - a capability, not a
    // platform: keyed on "what families does this target support", never "which platform".
    struct TargetProfile
    {
        bool bc = false;   // BC1-BC7 (desktop + desktop browsers)
        bool astc = false; // ASTC (mobile browsers)
        bool etc2 = false; // ETC2 (encoding not implemented; selecting it falls back to uncompressed)
    };

    // The always-warm host desktop profile (BC-capable).
    [[nodiscard]] inline TargetProfile DesktopProfile() noexcept
    {
        return TargetProfile{true, false, false};
    }

    // The mobile-web profile (ASTC-capable, no BC) - mobile browsers expose texture-compression-astc,
    // not -bc (asset-variants Decision 4). The web export cooks both this and the desktop BC variant.
    [[nodiscard]] inline TargetProfile MobileProfile() noexcept
    {
        return TargetProfile{false, true, false};
    }

    // Decision 5 policy table. Returns `uncompressed` when policy says do not compress (authored None,
    // small/UI textures <= 64px, HDR until BC6H, or a target with no supported family). `sRGB` selects
    // the *Srgb color formats; `hasAlpha` splits color into BC1 (opaque) vs BC7 (alpha).
    // `multiChannel` = the source carries distinct channel content (the cook's cheap sniff):
    // a MULTICHANNEL Mask (packed ORM/ARM) cooks BC7-linear - BC4 would keep R only and
    // silently drop roughness/metallic. Normal cooks BC7-linear (NOT BC5): the shaders decode
    // rgb * 2 - 1, and BC5's missing B samples as 0 -> tangent z = -1, visibly broken shading
    // (the chess-set white-normals bug). BC5 + shader z-reconstruction is the deferred
    // quality step - do not ship BC5 normals until the shaders reconstruct.
    [[nodiscard]] rhi::TextureFormat ResolveCompressedFormat(TextureUsage usage, bool sRGB, bool hasAlpha,
                                                             bool multiChannel,
                                                             CompressionChoice choice, u32 width,
                                                             u32 height, const TargetProfile& profile,
                                                             rhi::TextureFormat uncompressed) noexcept;

    // Channel-content sniff for the Mask guard (and the editor's Cooks-to preview):
    // true when any texel's G or B deviates from R by more than `tolerance` (default 8
    // absorbs JPEG chroma noise on gray sources). A packed ORM/ARM reads true; a gray
    // rough/AO/height map reads false.
    [[nodiscard]] bool HasDistinctChannels(const u8* rgba, u32 width, u32 height,
                                           u8 tolerance = 8) noexcept;

    // Encode one mip level of tightly-packed RGBA8 pixels (`width*height*4` bytes) to `format`'s
    // block-compressed bytes. `format` MUST be a BC format this build supports (BC1/3/4/5/7); returns
    // empty otherwise. Edge blocks on NPOT/small levels are clamp-padded. `quality` 0..255 maps to the
    // encoder effort (Default ~ mid, Quality ~ max).
    // `jobs`: the block rows fan out over it (ParallelFor; the caller participates), else the
    // encode runs inline. Byte-identical either way - every block is encoded on its own.
    [[nodiscard]] Array<byte> EncodeBlockCompressed(const u8* rgba, u32 width, u32 height,
                                                    rhi::TextureFormat format, u8 quality,
                                                    JobSystem* jobs = nullptr);

    // Bytes one mip level of a BC `format` occupies (4x4 block-ceil), for the exact-size cook assertion.
    [[nodiscard]] usize BlockCompressedSize(rhi::TextureFormat format, u32 width, u32 height) noexcept;

    // Encode one level of tightly-packed RGBA32F pixels to BC6H (unsigned half float, the
    // BC6HRGBUfloat format): 16 bytes per 4x4 block, edge blocks clamp-replicated. Alpha is
    // dropped (BC6H is RGB); negative, NaN and out-of-half-range values clamp - radiance maps
    // are non-negative by construction, so the signed variant stays unplumbed. `quality`
    // 0..255 buys endpoint refinement passes. In-house encoder (Bc6hEncoderImpl.cpp): the
    // single-region mode the real-time encoders use, least-squares refined - the plan's
    // "vendor bc6h_enc from bc7enc_rdo" turned out not to exist upstream.
    [[nodiscard]] Array<byte> EncodeBlockCompressedHdr(const f32* rgba, u32 width, u32 height,
                                                       u8 quality);

    // Register TextureUsage + CompressionChoice enum reflection (by-name), so the generic asset page
    // renders them as dropdowns. Idempotent; call from the asset's registration. (Bodies live in the
    // reflection impl unit - REFLECT_* bodies stay out of this interface, GCC module hygiene.)
    void RegisterCompressionReflection();
}
