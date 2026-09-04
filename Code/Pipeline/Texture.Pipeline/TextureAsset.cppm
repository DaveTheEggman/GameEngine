// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Texture - the `foundation.texture.editor` module (tooling).
//
// Source-side texture authoring + cook:
//   * TextureAsset (pipeline::Asset): references an image file + the GPU-texture
//     intent (color space, shape, sampler state). Presets mirror Sedulous's.
//   * TextureAssetBuilder (DefaultAssetBuilder): cooks a TextureAsset into a
//     runtime TextureResource - decode the file, resolve the RHI format from the
//     pixel format + color space, write the cooked record + "data" pixel stream.
//   * TextureImporter: an authoring helper that produces a TextureAsset for an
//     image file with a sensible preset (2D / equirectangular sky).
//
// Never linked by the runtime (an authoring/cook-seam helper). Cubemap import loads + validates +
// combines 6 face files.
// TODO: cooking a cubemap TextureResource through the builder is not implemented.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include <initializer_list>

export module texture.pipeline;

import foundation.core;
import pipeline.core;
import pipeline.importer;
import foundation.rhi;
import foundation.texture;
import foundation.texture.resource;
import foundation.image;
import foundation.image.io;
import foundation.content;
import texture.compression;

using namespace foundation::core;
using namespace foundation::texture;
namespace rhi = foundation::rhi;

export namespace pipeline{
    namespace image = foundation::image;
    namespace content = foundation::content;

    // Source asset: an image file + how it should become a GPU texture.
    class TextureAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(TextureAsset, pipeline::Asset)
    public:
        image::ImageColorSpace colorSpace = image::ImageColorSpace::Srgb;
        // Embedded mode (model imports): fileName empty + width/height set; the RGBA8 pixels
        // live in the source instance's "pixels" data stream instead of an external file.
        u32 embeddedWidth = 0;
        u32 embeddedHeight = 0;
        TextureShape shape = TextureShape::Texture2D;
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapU = TextureWrap::Repeat;
        TextureWrap wrapV = TextureWrap::Repeat;
        TextureWrap wrapW = TextureWrap::Repeat;
        bool generateMipmaps = true;
        f32 anisotropy = 1.0f;
        // Block-compression authoring. Editor-data only: the builder feeds these
        // to the policy table to pick the cooked rhi::TextureFormat; the runtime never sees them.
        texcomp::TextureUsage usage = texcomp::TextureUsage::Color;
        texcomp::CompressionChoice compression = texcomp::CompressionChoice::Default;
        // Provenance, DISPLAY-ONLY: where an embedded fan-out texture came from (the model's
        // image uri, e.g. "textures/board_arm_4k.jpg"). Never drives loading - the embedded
        // vs file branch keys on fileName/embeddedWidth, and this must stay out of it.
        String sourceHint;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName
            foundation::core::Serialize(ar, "colorSpace", colorSpace);
            // v2 (asset-variants): usage + compression. The XML serializer is STRICT (a
            // missing key fails the whole payload), so pre-variants envelopes MUST skip
            // these reads - unconditional reads broke every pre-variants texture asset
            // (ReadObject null: pages and thumbnails alike). v<2 keeps the defaults.
            if (ar.Version() >= 2)
            {
                foundation::core::Serialize(ar, "usage", usage);
                foundation::core::Serialize(ar, "compression", compression);
            }
            foundation::core::Serialize(ar, "embeddedWidth", embeddedWidth);
            foundation::core::Serialize(ar, "embeddedHeight", embeddedHeight);
            foundation::core::Serialize(ar, "shape", shape);
            foundation::core::Serialize(ar, "minFilter", minFilter);
            foundation::core::Serialize(ar, "magFilter", magFilter);
            foundation::core::Serialize(ar, "wrapU", wrapU);
            foundation::core::Serialize(ar, "wrapV", wrapV);
            foundation::core::Serialize(ar, "wrapW", wrapW);
            foundation::core::Serialize(ar, "generateMipmaps", generateMipmaps);
            foundation::core::Serialize(ar, "anisotropy", anisotropy);
            if (ar.Version() >= 3) // v3: display-only provenance for embedded fan-out textures
            {
                foundation::core::Serialize(ar, "sourceHint", sourceHint);
            }
        }

        // Profiles (Sedulous-derived samplers + the usage/colorSpace pair): each configures the
        // WHOLE story coherently - sampler, shape, usage, and the color space usage implies.
        // The page's profile buttons and the importer helpers share these one definitions.
        void SetupForUI()
        {
            usage = texcomp::TextureUsage::Color;
            colorSpace = image::ImageColorSpace::Srgb;
            shape = TextureShape::Texture2D;
            minFilter = TextureFilter::Linear;
            magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge;
            wrapV = TextureWrap::ClampToEdge;
            generateMipmaps = false;
            anisotropy = 1.0f;
        }
        void SetupForSprite()
        {
            usage = texcomp::TextureUsage::Color;
            colorSpace = image::ImageColorSpace::Srgb;
            shape = TextureShape::Texture2D;
            minFilter = TextureFilter::Nearest;
            magFilter = TextureFilter::Nearest;
            wrapU = TextureWrap::ClampToEdge;
            wrapV = TextureWrap::ClampToEdge;
            generateMipmaps = false;
            anisotropy = 1.0f;
        }
        void SetupFor3D()
        {
            usage = texcomp::TextureUsage::Color;
            colorSpace = image::ImageColorSpace::Srgb;
            shape = TextureShape::Texture2D;
            minFilter = TextureFilter::MipmapLinear;
            magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::Repeat;
            wrapV = TextureWrap::Repeat;
            generateMipmaps = true;
            anisotropy = 16.0f;
        }
        void SetupForEquirectangularSkybox()
        {
            usage = texcomp::TextureUsage::HDR;
            colorSpace = image::ImageColorSpace::Linear;
            shape = TextureShape::Texture2D;
            minFilter = TextureFilter::Linear;
            magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge;
            wrapV = TextureWrap::ClampToEdge;
            wrapW = TextureWrap::ClampToEdge;
            generateMipmaps = false;
            anisotropy = 1.0f;
        }
        // A tangent-space normal map: the 3D sampler + the data semantics (linear, BC5 policy).
        void SetupForNormalMap()
        {
            SetupFor3D();
            usage = texcomp::TextureUsage::Normal;
            colorSpace = image::ImageColorSpace::Linear;
        }
        // A data mask (roughness / AO / height / coverage): 3D sampler + linear single-channel policy.
        void SetupForDataMask()
        {
            SetupFor3D();
            usage = texcomp::TextureUsage::Mask;
            colorSpace = image::ImageColorSpace::Linear;
        }
        void SetupForCubemapSkybox()
        {
            usage = texcomp::TextureUsage::HDR;
            colorSpace = image::ImageColorSpace::Linear;
            shape = TextureShape::Cubemap;
            minFilter = TextureFilter::Linear;
            magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge;
            wrapV = TextureWrap::ClampToEdge;
            wrapW = TextureWrap::ClampToEdge;
            generateMipmaps = false;
            anisotropy = 1.0f;
        }
    };

    // Authoring helper: configure a TextureAsset for an image file with a preset.
    // (Asset is a non-copyable Object, so the result is filled in place.)
    class TextureImporter
    {
    public:
        // A standard 2D texture (3D preset: mips + anisotropy).
        static void Import2D(StringView path, image::ImageColorSpace colorSpace,
                             TextureAsset& outAsset)
        {
            outAsset.fileName = foundation::vfs::SourcePath(path);
            outAsset.SetupFor3D();
            outAsset.colorSpace = colorSpace;
        }

        // An HDR equirectangular sky (linear, clamped, no mips).
        static void ImportEquirectangular(StringView path, TextureAsset& outAsset)
        {
            outAsset.fileName = foundation::vfs::SourcePath(path);
            outAsset.SetupForEquirectangularSkybox();
        }

        // A cubemap sky from 6 face files (the first is stored as the asset source).
        static void ImportCubemap(StringView firstFacePath, TextureAsset& outAsset)
        {
            outAsset.fileName = foundation::vfs::SourcePath(firstFacePath);
            outAsset.SetupForCubemapSkybox();
        }

        // Load 6 cubemap faces into one combined buffer, faces concatenated in +X,-X,+Y,-Y,+Z,-Z order
        // (the layout a 6-layer cube texture expects). All faces must be square, the same size, and the
        // same format; the caller supplies the explicit paths. `outPixels` = 6 * faceSize*faceSize*bpp.
        [[nodiscard]] static Status LoadCubemap(Span<const StringView> facePaths,
                                                Array<u8>& outPixels, u32& outFaceSize)
        {
            if (facePaths.Size() != 6)
            {
                return ErrorCode::InvalidArgument;
            }
            image::Image faces[6];
            u32 faceSize = 0;
            usize faceBytes = 0;
            for (usize i = 0; i < 6; ++i)
            {
                if (!image::io::LoadImage(facePaths[i], faces[i]).IsOk())
                {
                    return ErrorCode::Unknown;
                }
                if (faces[i].Width() != faces[i].Height())
                {
                    return ErrorCode::Unknown;
                } // cube faces are square
                if (i == 0)
                {
                    faceSize = faces[0].Width();
                    faceBytes = faces[0].PixelData().Size();
                }
                else if (faces[i].Width() != faceSize || faces[i].PixelData().Size() != faceBytes ||
                         faces[i].Format() != faces[0].Format())
                {
                    return ErrorCode::Unknown;
                } // all faces must match
            }
            if (faceSize == 0 || faceBytes == 0)
            {
                return ErrorCode::Unknown;
            }
            outPixels.Resize(faceBytes * 6u);
            for (usize i = 0; i < 6; ++i)
            {
                MemCopy(outPixels.Data() + faceBytes * i, faces[i].PixelData().Data(), faceBytes);
            }
            outFaceSize = faceSize;
            return Status{};
        }

        // Given ONE face path (e.g. ".../sky_px.png"), derive all 6 face paths by matching a common
        // naming convention (px/nx/..., _posx/..., right/left/...) and rebuilding the set in
        // +X,-X,+Y,-Y,+Z,-Z order. Pure string derivation (no filesystem); pair with LoadCubemap, which
        // validates the files actually load. Returns Unknown if the path matches no known convention.
        [[nodiscard]] static Status DetectCubemapFaces(StringView oneFacePath,
                                                       Array<String>& outPaths)
        {
            const StringView dir = PathParent(oneFacePath);
            const StringView stem = PathStem(oneFacePath);
            const StringView ext = PathExtension(oneFacePath);
            static const StringView conv[5][6] = {
                {u8"px", u8"nx", u8"py", u8"ny", u8"pz", u8"nz"},
                {u8"_px", u8"_nx", u8"_py", u8"_ny", u8"_pz", u8"_nz"},
                {u8"_posx", u8"_negx", u8"_posy", u8"_negy", u8"_posz", u8"_negz"},
                {u8"_right", u8"_left", u8"_top", u8"_bottom", u8"_front", u8"_back"},
                {u8"right", u8"left", u8"top", u8"bottom", u8"front", u8"back"},
            };
            for (const auto& c : conv)
            {
                int matched = -1;
                for (int i = 0; i < 6; ++i)
                {
                    if (EndsWithCI(stem, c[i]))
                    {
                        matched = i;
                        break;
                    }
                }
                if (matched < 0)
                {
                    continue;
                }
                const StringView prefix = stem.SubStr(0, stem.Size() - c[matched].Size());
                outPaths.Clear();
                for (int i = 0; i < 6; ++i)
                {
                    String name{prefix};
                    name.Append(c[i]);
                    name.Append(ext);
                    outPaths.PushBack(dir.IsEmpty() ? name : PathJoin(dir, name.AsView()));
                }
                return Status{};
            }
            return ErrorCode::Unknown;
        }

    private:
        [[nodiscard]] static bool EndsWithCI(StringView s, StringView suffix) noexcept
        {
            if (s.Size() < suffix.Size())
            {
                return false;
            }
            const usize off = s.Size() - suffix.Size();
            for (usize i = 0; i < suffix.Size(); ++i)
            {
                utf8char a = s[off + i], b = suffix[i];
                if (a >= u8'A' && a <= u8'Z')
                {
                    a = static_cast<utf8char>(a + 32);
                }
                if (b >= u8'A' && b <= u8'Z')
                {
                    b = static_cast<utf8char>(b + 32);
                }
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }
    };

    // Cooks a TextureAsset -> TextureResource (decode + resolve RHI format ->
    // cooked record + "data" pixel stream).
    class TextureAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        // v2 (2026-08-12): the "data" payload may carry a FULL MIP CHAIN (levels
        // concatenated, mipLevels in the record) instead of always level 0 only.
        // v3 (2026-08-15): the "data" payload may be BLOCK-COMPRESSED (BCn) when the asset's
        // usage/compression + target profile select it; resource.format then
        // carries a BC format. Same-input output changed -> bump forces the re-cook.
        [[nodiscard]] u32 Version() const override { return 4; } // v4: Normal->BC7-linear + the multichannel-Mask guard

        // Textures are THE platform-variant producer: BC on desktop, ASTC on mobile-web from the same
        // source. The cook salts this builder's recipe by target + cooks per DB.
        [[nodiscard]] pipeline::BuildVariance Variance() const override
        {
            return pipeline::BuildVariance::PlatformVariant;
        }

        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &TextureAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &TextureResource::StaticType();
        }

        // Embedded-mode textures read the "pixels" sidecar stream - declare it so the recipe
        // hash chains its bytes (the envelope hash doesn't cover sidecars).
        void ScanDependencies(const pipeline::Asset& asset,
                              pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const TextureAsset& ta = static_cast<const TextureAsset&>(asset);
            if (ta.fileName.IsEmpty() && ta.embeddedWidth > 0)
            {
                out.sourceStreams.PushBack(String(u8"pixels"));
            }
            // Cubemaps: fileName is the +X face (the implicit dep); the other 5 faces must
            // chain into the recipe hash too, or editing one never re-cooks the cube.
            if (ta.shape == TextureShape::Cubemap && !ta.fileName.IsEmpty())
            {
                Array<String> faces;
                if (TextureImporter::DetectCubemapFaces(ta.fileName.View(), faces).IsOk())
                {
                    for (usize i = 0; i < faces.Size(); ++i)
                    {
                        if (faces[i].AsView() != ta.fileName.View())
                        {
                            out.files.PushBack(foundation::vfs::SourcePath(faces[i].AsView()));
                        }
                    }
                }
            }
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const TextureAsset& ta =
                static_cast<const TextureAsset&>(asset); // guarded by AssetType()
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }

            // Embedded mode: the pixels stream IS the decoded RGBA8 image (model imports).
            if (ta.fileName.IsEmpty() && ta.embeddedWidth > 0 && ta.embeddedHeight > 0)
            {
                return BuildEmbedded(ta, ctx);
            }

            // Cube-shaped assets load 6 face files (fileName = the +X face; the rest derive
            // from its naming convention) and cook them concatenated +X,-X,+Y,-Y,+Z,-Z.
            if (ta.shape == TextureShape::Cubemap)
            {
                return BuildCubemap(ta, ctx);
            }

            Result<Array<byte>> bytes = ReadSourceBytes(ctx, ta.fileName.View());
            if (!bytes.HasValue())
            {
                return Status{bytes.Error()};
            }
            image::Image image;
            const Status loaded = image::io::LoadImageFromMemory(
                Span<const u8>(reinterpret_cast<const u8*>(bytes.Value().Data()),
                               bytes.Value().Size()),
                image);
            if (!loaded.IsOk())
            {
                return loaded;
            }

            TextureResource resource;
            resource.width = image.Width();
            resource.height = image.Height();
            resource.depthOrArrayLayers = 1;
            resource.mipLevels = 1;
            resource.format = TextureFormatUtils::Convert(image.Format(), ta.colorSpace);
            Array<byte> mipPixels; // level 0 (+ generated chain when asked) - RGBA8 2D only
            {
                const Span<const u8> px0 = image.PixelData();
                mipPixels.Resize(px0.Size());
                if (px0.Size() != 0)
                {
                    MemCopy(mipPixels.Data(), px0.Data(), px0.Size());
                }
                if (ta.generateMipmaps && ta.shape == TextureShape::Texture2D &&
                    image.Format() == image::PixelFormat::RGBA8)
                {
                    resource.mipLevels = AppendMipChain(
                        mipPixels, image.Width(), image.Height(),
                        ta.colorSpace == image::ImageColorSpace::Srgb);
                }
                // Block-compress the RGBA8 chain when the policy table says to (2D RGBA8 only).
                if (ta.shape == TextureShape::Texture2D && image.Format() == image::PixelFormat::RGBA8)
                {
                    MaybeCompress(mipPixels, image.Width(), image.Height(), resource.mipLevels,
                                  ta.colorSpace == image::ImageColorSpace::Srgb, ta.usage,
                                  ta.compression, ProfileFor(ctx), resource.format);
                }
            }
            resource.shape = ta.shape;
            resource.minFilter = ta.minFilter;
            resource.magFilter = ta.magFilter;
            resource.wrapU = ta.wrapU;
            resource.wrapV = ta.wrapV;
            resource.wrapW = ta.wrapW;
            resource.generateMipmaps = ta.generateMipmaps;
            resource.anisotropy = ta.anisotropy;

            const Status wrote = ctx.output->WriteObject(resource);
            if (!wrote.IsOk())
            {
                return wrote;
            }

            return ctx.output->WriteData(
                u8"data", Span<const byte>(mipPixels.Data(), mipPixels.Size()));
        }

    private:
        // === Block compression ==================================================================
        // Resolve the cooked format from the asset's authored usage/compression + this cook's target
        // profile (the always-BC desktop host), then, when the policy picks a BC format, encode
        // every RGBA8 mip level in `pixels` to block bytes IN PLACE. `format` is updated to the chosen
        // format (unchanged when policy declines - small/None/HDR/no-BC-family). `pixels` holds the
        // level-0..N-1 RGBA8 chain tightly concatenated; the compressed chain replaces it 1:1.
        // The compressed-family profile for a cook: the target's capabilities, or
        // the always-BC desktop host when no target is set (the editor dev loop).
        [[nodiscard]] static texcomp::TargetProfile ProfileFor(const pipeline::AssetBuildContext& ctx)
        {
            if (ctx.target == nullptr)
            {
                return texcomp::DesktopProfile();
            }
            return texcomp::TargetProfile{ctx.target->bc, ctx.target->astc, ctx.target->etc2};
        }

        static void MaybeCompress(Array<byte>& pixels, u32 width, u32 height, u32 mipLevels, bool srgb,
                                  texcomp::TextureUsage usage, texcomp::CompressionChoice choice,
                                  const texcomp::TargetProfile& profile, rhi::TextureFormat& format)
        {
            if (choice == texcomp::CompressionChoice::None)
            {
                return;
            }
            // hasAlpha drives Color -> BC1(opaque) vs BC7(alpha): scan level 0 for any non-opaque texel.
            bool hasAlpha = false;
            {
                const u8* p = reinterpret_cast<const u8*>(pixels.Data());
                const usize texels = static_cast<usize>(width) * height;
                for (usize i = 0; i < texels; ++i)
                {
                    if (p[i * 4 + 3] != 255)
                    {
                        hasAlpha = true;
                        break;
                    }
                }
            }
            // Channel sniff for the Mask guard: a packed ORM/ARM authored as Mask must
            // not cook channel-dropping BC4 (a gray rough/AO map still does).
            const bool multiChannel = texcomp::HasDistinctChannels(
                reinterpret_cast<const u8*>(pixels.Data()), width, height);
            const rhi::TextureFormat chosen = texcomp::ResolveCompressedFormat(
                usage, srgb, hasAlpha, multiChannel, choice, width, height, profile, format);
            if (!rhi::IsCompressed(chosen))
            {
                return; // policy declined - leave the RGBA8 chain + format as-is
            }
            const u8 quality = (choice == texcomp::CompressionChoice::Quality) ? 255u : 128u;

            Array<byte> out;
            usize srcOffset = 0;
            u32 w = width;
            u32 h = height;
            for (u32 level = 0; level < mipLevels; ++level)
            {
                const usize levelBytes = static_cast<usize>(w) * h * 4;
                const Array<byte> block = texcomp::EncodeBlockCompressed(
                    reinterpret_cast<const u8*>(pixels.Data() + srcOffset), w, h, chosen, quality);
                if (block.Size() == 0)
                {
                    return; // encode failed - keep the uncompressed chain (safe fallback)
                }
                const usize at = out.Size();
                out.Resize(at + block.Size());
                MemCopy(out.Data() + at, block.Data(), block.Size());
                srcOffset += levelBytes;
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
            pixels = Move(out);
            format = chosen;
        }

        // === Mip generation (2026-08-12: the missing middle of the mip plumbing - the flag,
        // the record field, and the per-level upload all existed; nothing ever BUILT a chain,
        // so every texture rendered at mip 0: Sponza's shimmer) =========================

        // sRGB <-> linear for the downsample: averaging must happen in LINEAR space or mips
        // darken (a 50% black/white checker must average to linear 0.5 = sRGB ~188, not 128).
        [[nodiscard]] static f32 SrgbToLinear(u8 v)
        {
            const f32 c = static_cast<f32>(v) / 255.0f;
            return c <= 0.04045f ? c / 12.92f : Pow((c + 0.055f) / 1.055f, 2.4f);
        }
        [[nodiscard]] static u8 LinearToSrgb(f32 c)
        {
            c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            const f32 encoded =
                c <= 0.0031308f ? c * 12.92f : 1.055f * Pow(c, 1.0f / 2.4f) - 0.055f;
            return static_cast<u8>(encoded * 255.0f + 0.5f);
        }

        /// Appends the full mip chain (levels 1..N, 2x2 box, clamped for odd dims) to
        /// `pixels`, which holds level 0 as tight RGBA8. Color channels of sRGB images
        /// filter in linear space; alpha (and everything in Linear images) averages
        /// directly. Returns the TOTAL level count including level 0.
        [[nodiscard]] static u32 AppendMipChain(Array<byte>& pixels, u32 width, u32 height,
                                                bool srgb)
        {
            u32 levels = 1;
            usize srcOffset = 0;
            u32 srcW = width;
            u32 srcH = height;
            while (srcW > 1 || srcH > 1)
            {
                const u32 dstW = srcW > 1 ? srcW / 2 : 1;
                const u32 dstH = srcH > 1 ? srcH / 2 : 1;
                const usize dstOffset = pixels.Size();
                pixels.Resize(dstOffset + static_cast<usize>(dstW) * dstH * 4);
                const u8* src = reinterpret_cast<const u8*>(pixels.Data() + srcOffset);
                u8* dst = reinterpret_cast<u8*>(pixels.Data() + dstOffset);
                for (u32 y = 0; y < dstH; ++y)
                {
                    const u32 y0 = y * 2;
                    const u32 y1 = y0 + 1 < srcH ? y0 + 1 : y0; // clamp odd edges
                    for (u32 x = 0; x < dstW; ++x)
                    {
                        const u32 x0 = x * 2;
                        const u32 x1 = x0 + 1 < srcW ? x0 + 1 : x0;
                        const u8* p00 = src + (static_cast<usize>(y0) * srcW + x0) * 4;
                        const u8* p01 = src + (static_cast<usize>(y0) * srcW + x1) * 4;
                        const u8* p10 = src + (static_cast<usize>(y1) * srcW + x0) * 4;
                        const u8* p11 = src + (static_cast<usize>(y1) * srcW + x1) * 4;
                        u8* out = dst + (static_cast<usize>(y) * dstW + x) * 4;
                        for (i32 c = 0; c < 4; ++c)
                        {
                            if (srgb && c < 3) // color channels filter in linear space
                            {
                                const f32 avg = (SrgbToLinear(p00[c]) + SrgbToLinear(p01[c]) +
                                                 SrgbToLinear(p10[c]) + SrgbToLinear(p11[c])) *
                                                0.25f;
                                out[c] = LinearToSrgb(avg);
                            }
                            else
                            {
                                out[c] = static_cast<u8>(
                                    (static_cast<u32>(p00[c]) + p01[c] + p10[c] + p11[c] + 2) / 4);
                            }
                        }
                    }
                }
                srcOffset = dstOffset;
                srcW = dstW;
                srcH = dstH;
                ++levels;
            }
            return levels;
        }

        // Cook a 6-face cubemap: derive the face paths from the +X face's naming convention,
        // load each through the VFS, validate (square, matching size/format), concatenate.
        [[nodiscard]] static Status BuildCubemap(const TextureAsset& ta,
                                                 pipeline::AssetBuildContext& ctx)
        {
            Array<String> facePaths;
            if (!TextureImporter::DetectCubemapFaces(ta.fileName.View(), facePaths).IsOk() ||
                facePaths.Size() != 6)
            {
                return Status{ErrorCode::InvalidArgument}; // fileName matches no face convention
            }

            image::Image faces[6];
            u32 faceSize = 0;
            usize faceBytes = 0;
            for (usize i = 0; i < 6; ++i)
            {
                Result<Array<byte>> bytes = ReadSourceBytes(ctx, facePaths[i].AsView());
                if (!bytes.HasValue())
                {
                    return Status{bytes.Error()};
                }
                const Status loaded = image::io::LoadImageFromMemory(
                    Span<const u8>(reinterpret_cast<const u8*>(bytes.Value().Data()),
                                   bytes.Value().Size()),
                    faces[i]);
                if (!loaded.IsOk())
                {
                    return loaded;
                }
                if (faces[i].Width() != faces[i].Height())
                {
                    return Status{ErrorCode::InvalidArgument};
                }
                if (i == 0)
                {
                    faceSize = faces[0].Width();
                    faceBytes = faces[0].PixelData().Size();
                }
                else if (faces[i].Width() != faceSize || faces[i].PixelData().Size() != faceBytes ||
                         faces[i].Format() != faces[0].Format())
                {
                    return Status{ErrorCode::InvalidArgument}; // all faces must match
                }
            }
            if (faceSize == 0 || faceBytes == 0)
            {
                return Status{ErrorCode::InvalidArgument};
            }

            TextureResource resource;
            resource.width = faceSize;
            resource.height = faceSize;
            resource.depthOrArrayLayers = 6;
            resource.mipLevels = 1;
            resource.format = TextureFormatUtils::Convert(faces[0].Format(), ta.colorSpace);
            resource.shape = ta.shape;
            resource.minFilter = ta.minFilter;
            resource.magFilter = ta.magFilter;
            resource.wrapU = ta.wrapU;
            resource.wrapV = ta.wrapV;
            resource.wrapW = ta.wrapW;
            resource.generateMipmaps = ta.generateMipmaps;
            resource.anisotropy = ta.anisotropy;

            const Status wrote = ctx.output->WriteObject(resource);
            if (!wrote.IsOk())
            {
                return wrote;
            }

            Array<byte> pixels;
            pixels.Resize(faceBytes * 6u);
            for (usize i = 0; i < 6; ++i)
            {
                MemCopy(pixels.Data() + faceBytes * i, faces[i].PixelData().Data(), faceBytes);
            }
            return ctx.output->WriteData(u8"data", Span<const byte>(pixels.Data(), pixels.Size()));
        }

        [[nodiscard]] static Status BuildEmbedded(const TextureAsset& ta,
                                                  pipeline::AssetBuildContext& ctx)
        {
            if (ctx.source == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            UniquePtr<IStream> stream = ctx.source->ReadData(u8"pixels");
            if (stream.Get() == nullptr)
            {
                return Status{ErrorCode::NotFound};
            }
            const i64 size = stream->Size();
            const i64 expected = static_cast<i64>(ta.embeddedWidth) * ta.embeddedHeight * 4;
            if (size != expected)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            Array<byte> pixels;
            pixels.Resize(static_cast<usize>(size));
            if (stream->Read(pixels.Data(), static_cast<u64>(size)) != static_cast<u64>(size))
            {
                return Status{ErrorCode::Unknown};
            }

            TextureResource resource;
            resource.width = ta.embeddedWidth;
            resource.height = ta.embeddedHeight;
            resource.depthOrArrayLayers = 1;
            resource.mipLevels = 1;
            resource.format = (ta.colorSpace == image::ImageColorSpace::Srgb)
                                  ? rhi::TextureFormat::RGBA8UnormSrgb
                                  : rhi::TextureFormat::RGBA8Unorm;
            if (ta.generateMipmaps && ta.shape == TextureShape::Texture2D)
            {
                resource.mipLevels =
                    AppendMipChain(pixels, ta.embeddedWidth, ta.embeddedHeight,
                                   ta.colorSpace == image::ImageColorSpace::Srgb);
            }
            if (ta.shape == TextureShape::Texture2D)
            {
                MaybeCompress(pixels, ta.embeddedWidth, ta.embeddedHeight, resource.mipLevels,
                              ta.colorSpace == image::ImageColorSpace::Srgb, ta.usage, ta.compression,
                              ProfileFor(ctx), resource.format);
            }
            resource.shape = ta.shape;
            resource.minFilter = ta.minFilter;
            resource.magFilter = ta.magFilter;
            resource.wrapU = ta.wrapU;
            resource.wrapV = ta.wrapV;
            resource.wrapW = ta.wrapW;
            resource.generateMipmaps = ta.generateMipmaps;
            resource.anisotropy = ta.anisotropy;

            const Status wrote = ctx.output->WriteObject(resource);
            if (!wrote.IsOk())
            {
                return wrote;
            }
            return ctx.output->WriteData(u8"data", Span<const byte>(pixels.Data(), pixels.Size()));
        }
    };

    // OS-file importer (editor drag-drop): copies the image into Sources/ and creates a
    // TextureAsset instance named after the file stem in the target group. Preset by intent:
    //   - .hdr                                  -> equirectangular sky (linear/clamped/no mips)
    //   - a cube-face name (sky_px.png etc.) with all 6 sibling faces present
    //                                           -> ONE cube asset (all 6 faces copied)
    //   - anything else                         -> the standard 3D preset
    // Usage inference from texture-pack name tokens, case-insensitive, matched anywhere in
    // the stem. A token counts only when what FOLLOWS it is the end, an underscore, or a digit
    // ("foo_nor_gl_4k" matches "_nor"; "my_armor" does not match "_arm" - the boundary rule
    // keeps ordinary words out). Unrecognized names infer Color.
    [[nodiscard]] inline texcomp::TextureUsage InferTextureUsage(StringView stem)
    {
        String lower;
        lower.Reserve(stem.Size());
        for (usize i = 0; i < stem.Size(); ++i)
        {
            utf8char c = stem[i];
            if (c >= u8'A' && c <= u8'Z')
            {
                c = static_cast<utf8char>(c + 32);
            }
            lower.Append(c);
        }
        const auto hasToken = [&lower](StringView token) -> bool
        {
            const StringView haystack = lower.AsView();
            if (haystack.Size() < token.Size())
            {
                return false;
            }
            for (usize at = 0; at + token.Size() <= haystack.Size(); ++at)
            {
                bool match = true;
                for (usize i = 0; i < token.Size(); ++i)
                {
                    if (haystack[at + i] != token[i])
                    {
                        match = false;
                        break;
                    }
                }
                if (!match)
                {
                    continue;
                }
                const usize next = at + token.Size();
                if (next >= haystack.Size() || haystack[next] == u8'_' ||
                    (haystack[next] >= u8'0' && haystack[next] <= u8'9'))
                {
                    return true;
                }
            }
            return false;
        };
        for (StringView t :
             {u8"_normal", u8"_norm", u8"_nrm", u8"_nor", u8"_ddn", u8"normalmap"})
        {
            if (hasToken(t))
            {
                return texcomp::TextureUsage::Normal;
            }
        }
        for (StringView t : {u8"_disp", u8"_height", u8"_mask", u8"_rough", u8"_ao", u8"_orm",
                             u8"_arm", u8"_metal"})
        {
            if (hasToken(t))
            {
                return texcomp::TextureUsage::Mask;
            }
        }
        return texcomp::TextureUsage::Color;
    }

    class TextureFileImporter final : public pipeline::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Texture"; }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            for (StringView ext : {u8"png", u8"jpg", u8"jpeg", u8"tga", u8"bmp", u8"hdr"})
            {
                if (extension == ext)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] pipeline::ImportPlan DescribeImport(StringView sourcePath,
                                                          const pipeline::ImportOptions*,
                                                          Object*) override
        {
            return pipeline::SingleAssetPlan(sourcePath); // one asset, named after the stem
        }

        [[nodiscard]] pipeline::ImportPlan StoredSelection(content::Group& group,
                                                           StringView sourcePath) override
        {
            return pipeline::SingleAssetStoredSelection(group, sourcePath, u8"TextureAsset");
        }

        [[nodiscard]] Result<content::Instance*>
        Import(StringView sourcePath, const pipeline::ImportContext& context,
               content::Group& group, const pipeline::ImportOptions* options, Object*,
               Array<pipeline::DeferredImportWrite>*) override
        {
            // Cubemap intent: the dropped file's stem matches a face convention (px/nx/...,
            // _posx/..., right/left/...) AND all 6 sibling faces exist beside it. Any one
            // face can be dropped; the asset stores the +X face (the rest derive at cook).
            {
                Array<String> facePaths;
                if (TextureImporter::DetectCubemapFaces(sourcePath, facePaths).IsOk())
                {
                    bool allPresent = facePaths.Size() == 6;
                    for (const String& face : facePaths)
                    {
                        if (!FileExists(face.AsView()))
                        {
                            allPresent = false;
                            break;
                        }
                    }
                    if (allPresent)
                    {
                        return ImportCube(facePaths, context, group);
                    }
                }
            }

            Result<String> fileName = pipeline::CopyIntoSources(context, sourcePath);
            if (!fileName.HasValue())
            {
                return Err(fileName.Error());
            }

            const StringView stem = pipeline::FileStemOf(fileName.Value().AsView());
            content::Instance* instance = group.CreateInstance(
                pipeline::SingleAssetName(options, stem), TextureAsset::StaticType());
            if (instance == nullptr)
            {
                return Err(ErrorCode::Unknown);
            }

            TextureAsset asset;
            asset.fileName = foundation::vfs::SourcePath(fileName.Value().AsView());
            if (pipeline::FileExtensionLower(sourcePath) == u8"hdr")
            {
                asset.SetupForEquirectangularSkybox(); // .hdr = an environment, not a surface map
            }
            else
            {
                // Usage inference from the universal texture-pack name tokens: the result is
                // just the stored fields - the page shows what was inferred and the author
                // corrects it like any edit. Unrecognized names keep the Color default.
                switch (InferTextureUsage(stem))
                {
                case texcomp::TextureUsage::Normal:
                    asset.SetupForNormalMap();
                    break;
                case texcomp::TextureUsage::Mask:
                    asset.SetupForDataMask();
                    break;
                default:
                    asset.SetupFor3D();
                    break;
                }
            }
            const Status written = instance->WriteObject(asset);
            if (!written.IsOk())
            {
                return Err(written.Code());
            }
            return instance;
        }

    private:
        // Copy all 6 faces into Sources/ and create ONE cube TextureAsset. fileName = the
        // +X face; the builder re-derives the face set from its naming convention at cook.
        [[nodiscard]] static Result<content::Instance*>
        ImportCube(const Array<String>& facePaths, const pipeline::ImportContext& context,
                   content::Group& group)
        {
            String posXName;
            for (usize i = 0; i < facePaths.Size(); ++i)
            {
                Result<String> copied =
                    pipeline::CopyIntoSources(context, facePaths[i].AsView());
                if (!copied.HasValue())
                {
                    return Err(copied.Error());
                }
                if (i == 0)
                {
                    posXName = copied.Value();
                }
            }

            // "sky_px" -> "sky" (strip the face suffix + a trailing separator); fall back to
            // the full stem when the convention leaves nothing.
            const StringView posXStem = pipeline::FileStemOf(posXName.AsView());
            Array<String> derived;
            String name;
            if (TextureImporter::DetectCubemapFaces(posXName.AsView(), derived).IsOk())
            {
                // The convention suffix is whatever the +X path ends with beyond the shared prefix.
                usize common = 0;
                const StringView a = derived[0].AsView();
                const StringView b = derived[1].AsView();
                while (common < a.Size() && common < b.Size() && a[common] == b[common])
                {
                    ++common;
                }
                StringView prefix = pipeline::FileStemOf(a.SubStr(0, common));
                while (!prefix.IsEmpty() && (prefix[prefix.Size() - 1] == utf8char('_') ||
                                             prefix[prefix.Size() - 1] == utf8char('-')))
                {
                    prefix = prefix.SubStr(0, prefix.Size() - 1);
                }
                name = String(prefix);
            }
            if (name.IsEmpty())
            {
                name = String(posXStem);
            }

            content::Instance* instance =
                group.CreateInstance(name.AsView(), TextureAsset::StaticType());
            if (instance == nullptr)
            {
                return Err(ErrorCode::Unknown);
            }
            TextureAsset asset;
            asset.fileName = foundation::vfs::SourcePath(posXName.AsView());
            asset.SetupForCubemapSkybox();
            const Status written = instance->WriteObject(asset);
            if (!written.IsOk())
            {
                return Err(written.Code());
            }
            return instance;
        }
    };

    // Registers TextureAsset for content-DB construction + deserialization. Also registers the
    // enum reflection its properties reference (owning modules; idempotent) so the generic asset
    // page can render enum-by-name dropdowns. TextureAsset's OWN reflection body (properties +
    // attributes) is TextureAsset::StaticType(), defined in TextureAssetImpl.cpp.
    inline void RegisterTextureAsset()
    {
        RegisterTextureReflection();          // TextureShape / TextureFilter / TextureWrap names
        image::RegisterImageReflection();     // ImageColorSpace names
        texcomp::RegisterCompressionReflection(); // TextureUsage / CompressionChoice names
        GlobalTypeRegistry().Register(TextureAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<TextureAsset>();
    }
}
