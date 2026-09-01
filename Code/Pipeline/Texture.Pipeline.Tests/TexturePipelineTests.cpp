// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Full GPU-texture asset pipeline: author a TextureAsset (image file + sampler
// intent) -> cook with TextureAssetBuilder into an output content DB -> load the
// cooked TextureResource and build a live GPU Texture via the device-backed
// factory (Null RHI backend). Also exercises the TextureImporter authoring helper.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include <initializer_list>
#include "Core/Reflection/Reflect.h"
import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.rhi;
import foundation.rhi.null;
import pipeline.core;
import editor.core;
import foundation.xml.serialization;
import pipeline.importer;
import foundation.image;
import foundation.image.io;
import foundation.texture;
import foundation.texture.resource;
import texture.pipeline;
import texture.compression;

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::texture;
namespace image = foundation::image;
namespace rhi = foundation::rhi;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"scratch_texpipe_src.png");
        FileDelete(u8"scratch_texpipe_out_db/diffuse.rasset");
        FileDelete(u8"scratch_texpipe_out_db/diffuse.data.bin");
        RemoveDirectory(u8"scratch_texpipe_out_db");
    }
}

TEST_CASE("texture.pipeline: TextureAsset -> cook -> GPU Texture")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    RemoveTree();

    // A known 2x2 RGBA source image on disk.
    {
        image::Image src(2, 2, image::PixelFormat::RGBA8);
        Span<u8> px = src.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i)
        {
            px.Data()[i] = static_cast<u8>(i * 5);
        }
        REQUIRE(
            image::io::SaveImage(src, u8"scratch_texpipe_src.png", image::io::ImageFileFormat::PNG)
                .IsOk());
    }

    NativeFileSystem outMount(u8"scratch_texpipe_out_db");
    Guid id;

    // --- cook (tooling): TextureAsset -> TextureResource in the output DB ---
    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());
        id = inst->Id();

        TextureAsset asset;
        asset.fileName = foundation::vfs::SourcePath(u8"scratch_texpipe_src.png");
        asset.SetupForUI(); // clamp, no mips
        asset.colorSpace = image::ImageColorSpace::Srgb;

        TextureAssetBuilder builder;
        REQUIRE(builder.AssetType() == &TextureAsset::StaticType());
        foundation::vfs::NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- runtime load: cooked TextureResource -> live GPU Texture (model A) ---
    rhi::null::NullDevice device{DefaultAllocator()};
    foundation::content::ContentDatabase outDb(outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    TextureFactory factory(device);
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->Width() == 2u);
    CHECK(tex->Height() == 2u);
    CHECK(tex->Format() == rhi::TextureFormat::RGBA8UnormSrgb); // sRGB resolved from color space
    CHECK(tex->GpuTexture() != nullptr);
    CHECK(tex->Sampler() != nullptr);

    RemoveTree();
}

TEST_CASE("texture.importer: produces a TextureAsset with the right preset")
{
    TextureAsset a;
    TextureImporter::Import2D(u8"art/brick.png", image::ImageColorSpace::Srgb, a);
    CHECK(a.fileName == String(u8"art/brick.png"));
    CHECK(a.colorSpace == image::ImageColorSpace::Srgb);
    CHECK(a.generateMipmaps); // 3D preset
    CHECK(a.minFilter == TextureFilter::MipmapLinear);

    TextureAsset sky;
    TextureImporter::ImportEquirectangular(u8"sky/dusk.hdr", sky);
    CHECK(sky.colorSpace == image::ImageColorSpace::Linear);
    CHECK(sky.wrapU == TextureWrap::ClampToEdge);
    CHECK_FALSE(sky.generateMipmaps);
}

TEST_CASE("texture.pipeline: builder fails on a missing source file")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_texpipe_out_db");
    foundation::content::ContentDatabase outDb(outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());

    TextureAsset asset;
    asset.fileName = foundation::vfs::SourcePath(u8"does_not_exist_xyz.png");
    TextureAssetBuilder builder;
    foundation::vfs::NativeFileSystem srcMount(u8".");
    pipeline::AssetBuildContext ctx;
    ctx.sources = &srcMount;
    ctx.output = inst;
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}

TEST_CASE("texture-import: drag-dropped file becomes a Sources copy + TextureAsset instance")
{
    RegisterTextureAsset();

    const StringView dir = u8"scratch_tex_import_project";
    auto cleanTree = [&]()
    {
        FileDelete(PathJoin(dir, u8"Project.xml"));
        FileDelete(PathJoin(dir, u8"Sources/brick.png"));
        FileDelete(PathJoin(dir, u8"Content/brick.xasset"));
        for (StringView sub : {u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache"})
        {
            RemoveDirectory(PathJoin(dir, sub));
        }
        RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(editor::EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<editor::EditorProject> project = editor::EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    // A loose "PNG" (the importer copies bytes + creates the asset; decoding happens at cook).
    const byte fakePng[6] = {byte{'P'}, byte{'N'}, byte{'G'}, byte{1}, byte{2}, byte{3}};
    REQUIRE(WriteFile(u8"brick.png", Span<const byte>(fakePng, 6)).IsOk());

    TextureFileImporter importer;
    CHECK(importer.Accepts(u8"png"));
    CHECK(importer.Accepts(u8"jpeg"));
    CHECK_FALSE(importer.Accepts(u8"gltf"));

    Result<foundation::content::Instance*> imported = importer.Import(
        u8"brick.png", pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(imported.HasValue());
    foundation::content::Instance* instance = imported.Value();
    REQUIRE(instance != nullptr);
    CHECK(instance->Name() == StringView(u8"brick"));
    CHECK(instance->TypeName() == StringView(u8"TextureAsset"));

    // The source is in Sources/ and the asset references it by mount-relative name.
    CHECK(FileExists(PathJoin(dir, u8"Sources/brick.png").AsView()));
    RefPtr<ISerializable> object = instance->ReadObject();
    auto* asset = Cast<TextureAsset>(object.Get());
    REQUIRE(asset != nullptr);
    CHECK(asset->fileName == StringView(u8"brick.png"));
    CHECK(asset->generateMipmaps); // the 3D preset
    CHECK(asset->usage == texcomp::TextureUsage::Color); // plain name = Color default

    FileDelete(u8"brick.png");
    cleanTree();
}

TEST_CASE("texture-import: a normal-map suffix imports as Normal usage + linear color space")
{
    RegisterTextureAsset();

    const StringView dir = u8"scratch_tex_import_nrm_project";
    auto cleanTree = [&]()
    {
        FileDelete(PathJoin(dir, u8"Project.xml"));
        FileDelete(PathJoin(dir, u8"Sources/wall_normal.png"));
        for (StringView sub : {u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache"})
        {
            RemoveDirectory(PathJoin(dir, sub));
        }
        RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(editor::EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<editor::EditorProject> project = editor::EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    const byte fakePng[6] = {byte{'P'}, byte{'N'}, byte{'G'}, byte{1}, byte{2}, byte{3}};
    REQUIRE(WriteFile(u8"wall_normal.png", Span<const byte>(fakePng, 6)).IsOk());

    TextureFileImporter importer;
    Result<foundation::content::Instance*> imported = importer.Import(
        u8"wall_normal.png", pipeline::ImportContext{project->SourcesRoot()},
        *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(imported.HasValue());
    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* asset = Cast<TextureAsset>(object.Get());
    REQUIRE(asset != nullptr);
    // The heuristic fired: a tangent normal cooks linear + BC5, never sRGB color.
    CHECK(asset->usage == texcomp::TextureUsage::Normal);
    CHECK(asset->colorSpace == image::ImageColorSpace::Linear);

    FileDelete(u8"wall_normal.png");
    cleanTree();
}

TEST_CASE("texture.pipeline: cubemap - 6 faces cook into one cube product (end to end)")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    const StringView faceNames[6] = {
        u8"scratch_texpipe_sky_px.png", u8"scratch_texpipe_sky_nx.png",
        u8"scratch_texpipe_sky_py.png", u8"scratch_texpipe_sky_ny.png",
        u8"scratch_texpipe_sky_pz.png", u8"scratch_texpipe_sky_nz.png",
    };
    auto scrub = [&]()
    {
        for (StringView f : faceNames)
        {
            FileDelete(f);
        }
        FileDelete(u8"scratch_texpipe_cube_db/sky.rasset");
        FileDelete(u8"scratch_texpipe_cube_db/sky.data.bin");
        RemoveDirectory(u8"scratch_texpipe_cube_db");
    };
    scrub();

    // 6 distinct 4x4 faces (each filled with its face index so the concatenation order shows).
    for (u32 f = 0; f < 6; ++f)
    {
        image::Image src(4, 4, image::PixelFormat::RGBA8);
        Span<u8> px = src.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i)
        {
            px.Data()[i] = static_cast<u8>(f * 10 + 1);
        }
        REQUIRE(image::io::SaveImage(src, faceNames[f], image::io::ImageFileFormat::PNG).IsOk());
    }

    // The +X face's naming convention derives the whole set (what the builder does at cook).
    {
        Array<String> derived;
        REQUIRE(TextureImporter::DetectCubemapFaces(faceNames[0], derived).IsOk());
        REQUIRE(derived.Size() == 6u);
        CHECK(derived[5].AsView() == faceNames[5]);
    }

    NativeFileSystem outMount(u8"scratch_texpipe_cube_db");
    Guid id;
    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"sky", TextureResource::StaticType());
        id = inst->Id();

        TextureAsset asset;
        asset.fileName = foundation::vfs::SourcePath(faceNames[0]); // the +X face
        asset.SetupForCubemapSkybox();

        // The recipe must chain ALL faces (editing -nz alone must re-cook the cube).
        TextureAssetBuilder builder;
        pipeline::AssetBuildContext scanCtx;
        pipeline::AssetDependencies deps;
        builder.ScanDependencies(asset, scanCtx, deps);
        CHECK(deps.files.Size() == 5u); // the 5 non-+X faces (fileName is the implicit dep)

        foundation::vfs::NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // Runtime: the factory builds a real cube (6 layers, cube view, per-face upload).
    rhi::null::NullDevice device{DefaultAllocator()};
    foundation::content::ContentDatabase outDb(outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    TextureFactory factory(device);
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->Width() == 4u);
    CHECK(tex->Height() == 4u);
    CHECK(tex->IsCube());
    CHECK(tex->Uid() != 0u);
    CHECK(tex->GpuTexture() != nullptr);

    scrub();
}

TEST_CASE("texture.pipeline: mip chain cook - counts, sizes, and sRGB-correct averaging")
{
    // The missing middle found 2026-08-12: generateMipmaps/mipLevels/per-level upload all
    // existed, nothing BUILT a chain, everything rendered at mip 0 (Sponza's shimmer). Pins:
    // (1) chain shape: a 4x4 cooks 3 levels, payload = (16+4+1)*4 bytes;
    // (2) sRGB correctness: a black/white checker's mip must average in LINEAR space -
    //     sRGB ~188 - not the naive byte average 127;
    // (3) Linear images average bytes directly (a data map's mip stays 127/128);
    // (4) odd dims clamp (5x3 -> 2x1 -> 1x1).
    RegisterTextureResource();
    RegisterTextureAsset();

    const auto cookEmbedded = [](u32 w, u32 h, image::ImageColorSpace cs,
                                 Function<u8(u32, u32, i32)> pixelAt, StringView dbDir,
                                 u32& outMipLevels, Array<u8>& outPayload) {
        (void)RemoveDirectoryRecursive(dbDir);
        NativeFileSystem srcMount(dbDir);
        foundation::content::ContentDatabase db(
            srcMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* srcInst = db.RootGroup()->CreateInstance(u8"src", TextureAsset::StaticType());
        auto* outInst = db.RootGroup()->CreateInstance(u8"out", TextureResource::StaticType());

        Array<byte> pixels;
        pixels.Resize(static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y)
        {
            for (u32 x = 0; x < w; ++x)
            {
                for (i32 c = 0; c < 4; ++c)
                {
                    pixels[(static_cast<usize>(y) * w + x) * 4 + static_cast<usize>(c)] =
                        static_cast<byte>(pixelAt(x, y, c));
                }
            }
        }
        TextureAsset asset;
        asset.embeddedWidth = w;
        asset.embeddedHeight = h;
        asset.colorSpace = cs;
        asset.generateMipmaps = true;
        REQUIRE(srcInst->WriteObject(asset).IsOk());
        REQUIRE(srcInst->WriteData(u8"pixels", Span<const byte>(pixels.Data(), pixels.Size()))
                    .IsOk());

        TextureAssetBuilder builder;
        pipeline::AssetBuildContext ctx;
        ctx.source = srcInst;
        ctx.output = outInst;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        RefPtr<ISerializable> object = outInst->ReadObject();
        auto* res = Cast<TextureResource>(object.Get());
        REQUIRE(res != nullptr);
        outMipLevels = res->mipLevels;
        UniquePtr<IStream> data = outInst->ReadData(u8"data");
        REQUIRE(data);
        outPayload.Resize(static_cast<usize>(data->Size()));
        REQUIRE(data->Read(outPayload.Data(), static_cast<u64>(outPayload.Size())) ==
                static_cast<u64>(outPayload.Size()));
        (void)RemoveDirectoryRecursive(dbDir);
    };

    // (1)+(2): 4x4 sRGB checkerboard (0 / 255 alternating).
    {
        u32 mipLevels = 0;
        Array<u8> payload;
        cookEmbedded(4, 4, image::ImageColorSpace::Srgb,
                     Function<u8(u32, u32, i32)>{[](u32 x, u32 y, i32 c)
                                                 { return c == 3 ? u8{255}
                                                                 : (((x + y) & 1) ? u8{255}
                                                                                  : u8{0}); }},
                     u8"scratch_texpipe_mips_srgb", mipLevels, payload);
        CHECK(mipLevels == 3u);
        CHECK(payload.Size() == (16u + 4u + 1u) * 4u);
        // mip1 first texel: 2x2 checker averages to linear 0.5 -> sRGB ~188 (NOT 127).
        const u8 mip1 = payload[16u * 4u];
        CHECK(mip1 >= 186);
        CHECK(mip1 <= 190);
        // Alpha averages directly (all 255).
        CHECK(payload[16u * 4u + 3u] == 255);
    }

    // (3): the SAME checker as a Linear data map keeps the straight byte average.
    {
        u32 mipLevels = 0;
        Array<u8> payload;
        cookEmbedded(4, 4, image::ImageColorSpace::Linear,
                     Function<u8(u32, u32, i32)>{[](u32 x, u32 y, i32 c)
                                                 { return c == 3 ? u8{255}
                                                                 : (((x + y) & 1) ? u8{255}
                                                                                  : u8{0}); }},
                     u8"scratch_texpipe_mips_linear", mipLevels, payload);
        CHECK(mipLevels == 3u);
        const u8 mip1 = payload[16u * 4u];
        CHECK(mip1 >= 127);
        CHECK(mip1 <= 128);
    }

    // (4): odd dims clamp down the chain: 5x3 -> 2x1 -> 1x1.
    {
        u32 mipLevels = 0;
        Array<u8> payload;
        cookEmbedded(5, 3, image::ImageColorSpace::Linear,
                     Function<u8(u32, u32, i32)>{[](u32, u32, i32) { return u8{200}; }},
                     u8"scratch_texpipe_mips_npot", mipLevels, payload);
        CHECK(mipLevels == 3u);
        CHECK(payload.Size() == (15u + 2u + 1u) * 4u);
    }
}

TEST_CASE("texture.pipeline: block compression cook - format policy + exact cooked-DB size drop")
{
    // The cook block-compresses the RGBA8 mip chain when the asset's authored
    // usage/compression + the desktop (BC) profile select a BC format. Pins:
    // (1) usage/compression -> the cooked resource.format; (2) the "data" payload is EXACTLY the sum
    // of per-level block bytes; (3) the size drop vs uncompressed is real (BC1 ~8x, BC7 ~4x).
    RegisterTextureResource();
    RegisterTextureAsset();

    // Cook a 128x128 embedded image (>64px so it clears the small-texture escape hatch) with the
    // given authoring, returning the cooked format + "data" payload size.
    const auto cook = [](texcomp::TextureUsage usage, texcomp::CompressionChoice choice,
                         image::ImageColorSpace cs, bool alpha, StringView dbDir,
                         rhi::TextureFormat& outFormat, u32& outMips, usize& outPayload) {
        const u32 w = 128, h = 128;
        (void)RemoveDirectoryRecursive(dbDir);
        NativeFileSystem srcMount(dbDir);
        foundation::content::ContentDatabase db(
            srcMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* srcInst = db.RootGroup()->CreateInstance(u8"src", TextureAsset::StaticType());
        auto* outInst = db.RootGroup()->CreateInstance(u8"out", TextureResource::StaticType());

        Array<byte> pixels;
        pixels.Resize(static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y)
        {
            for (u32 x = 0; x < w; ++x)
            {
                byte* p = pixels.Data() + (static_cast<usize>(y) * w + x) * 4;
                p[0] = static_cast<byte>((x * 255) / (w - 1));
                p[1] = static_cast<byte>((y * 255) / (h - 1));
                p[2] = static_cast<byte>(((x + y) * 255) / (w + h - 2));
                p[3] = alpha ? static_cast<byte>((x * 255) / (w - 1)) : static_cast<byte>(255);
            }
        }
        TextureAsset asset;
        asset.embeddedWidth = w;
        asset.embeddedHeight = h;
        asset.colorSpace = cs;
        asset.generateMipmaps = true;
        asset.usage = usage;
        asset.compression = choice;
        REQUIRE(srcInst->WriteObject(asset).IsOk());
        REQUIRE(srcInst->WriteData(u8"pixels", Span<const byte>(pixels.Data(), pixels.Size())).IsOk());

        TextureAssetBuilder builder;
        pipeline::AssetBuildContext ctx;
        ctx.source = srcInst;
        ctx.output = outInst;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        RefPtr<ISerializable> object = outInst->ReadObject();
        auto* res = Cast<TextureResource>(object.Get());
        REQUIRE(res != nullptr);
        outFormat = res->format;
        outMips = res->mipLevels;
        UniquePtr<IStream> data = outInst->ReadData(u8"data");
        REQUIRE(data);
        outPayload = static_cast<usize>(data->Size());
        (void)RemoveDirectoryRecursive(dbDir);
    };

    // Sum of per-level block bytes for a full 128x128 chain of `format` (the exact cooked size).
    const auto expectedCompressed = [](rhi::TextureFormat format, u32 levels) {
        usize total = 0;
        u32 w = 128, h = 128;
        for (u32 i = 0; i < levels; ++i)
        {
            total += rhi::CompressedLevelBytes(format, w, h);
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        return total;
    };

    rhi::TextureFormat fmt{};
    u32 mips = 0;
    usize payload = 0;

    // (1) Color, opaque, sRGB, Default -> BC1 sRGB; payload = exact BC1 block bytes.
    cook(texcomp::TextureUsage::Color, texcomp::CompressionChoice::Default,
         image::ImageColorSpace::Srgb, /*alpha*/ false, u8"scratch_texpipe_bc1", fmt, mips, payload);
    CHECK(fmt == rhi::TextureFormat::BC1RGBAUnormSrgb);
    CHECK(payload == expectedCompressed(rhi::TextureFormat::BC1RGBAUnormSrgb, mips));

    // The uncompressed RGBA8 chain, for the size-drop comparison.
    usize rawPayload = 0;
    cook(texcomp::TextureUsage::Color, texcomp::CompressionChoice::None, image::ImageColorSpace::Srgb,
         false, u8"scratch_texpipe_raw", fmt, mips, rawPayload);
    CHECK(fmt == rhi::TextureFormat::RGBA8UnormSrgb);
    // BC1 is 4 bits/texel vs 32 -> the cooked payload is ~8x smaller. Guard a real, large drop.
    CHECK(payload * 6u < rawPayload);

    // (2) Color, alpha -> BC7; Quality on an opaque image also forces BC7.
    cook(texcomp::TextureUsage::Color, texcomp::CompressionChoice::Default,
         image::ImageColorSpace::Srgb, /*alpha*/ true, u8"scratch_texpipe_bc7a", fmt, mips, payload);
    CHECK(fmt == rhi::TextureFormat::BC7RGBAUnormSrgb);
    CHECK(payload == expectedCompressed(rhi::TextureFormat::BC7RGBAUnormSrgb, mips));

    cook(texcomp::TextureUsage::Color, texcomp::CompressionChoice::Quality,
         image::ImageColorSpace::Linear, false, u8"scratch_texpipe_bc7q", fmt, mips, payload);
    CHECK(fmt == rhi::TextureFormat::BC7RGBAUnorm);

    // (3) Normal -> BC5, Mask -> BC4 (linear, ignore color space).
    cook(texcomp::TextureUsage::Normal, texcomp::CompressionChoice::Default,
         image::ImageColorSpace::Linear, false, u8"scratch_texpipe_bc5", fmt, mips, payload);
    CHECK(fmt == rhi::TextureFormat::BC5RGUnorm);
    CHECK(payload == expectedCompressed(rhi::TextureFormat::BC5RGUnorm, mips));

    cook(texcomp::TextureUsage::Mask, texcomp::CompressionChoice::Default,
         image::ImageColorSpace::Linear, false, u8"scratch_texpipe_bc4", fmt, mips, payload);
    CHECK(fmt == rhi::TextureFormat::BC4RUnorm);
    CHECK(payload == expectedCompressed(rhi::TextureFormat::BC4RUnorm, mips));
}

TEST_CASE("texture.asset: pre-variants payloads (no usage/compression keys) still deserialize")
{
    // The XML serializer is STRICT - a missing key fails the whole payload - and the
    // asset-variants fields were briefly read unconditionally, breaking ReadObject for EVERY
    // pre-variants texture envelope (pages + thumbnails alike). The fields are v2-gated now:
    // a v0 payload (exactly the old files' shape for these fields) must parse and keep the
    // defaults; a CURRENT versioned round-trip must keep authored values.
    using foundation::xml::XmlSerializerFactory;

    // Old-shape payload: serialize WITHOUT a version scope (Version() == 0 skips the v2
    // fields on write - producing the pre-variants key set).
    foundation::core::MemoryStream oldBytes;
    {
        auto ctx = XmlSerializerFactory()(oldBytes, foundation::core::SerializeMode::Write);
        REQUIRE(ctx.Get() != nullptr);
        pipeline::TextureAsset asset;
        asset.generateMipmaps = true; // a non-default pre-variants field must survive
        asset.Serialize(*ctx->serializer);
        REQUIRE(ctx->serializer->IsOk());
        ctx->Flush(oldBytes);
    }
    {
        (void)oldBytes.Seek(0, foundation::core::SeekOrigin::Begin);
        auto ctx = XmlSerializerFactory()(oldBytes, foundation::core::SerializeMode::Read);
        REQUIRE(ctx.Get() != nullptr);
        pipeline::TextureAsset asset;
        asset.usage = texcomp::TextureUsage::Normal; // must RESET to... no: v0 read skips the
        asset.usage = texcomp::TextureUsage::Color;  // fields entirely - defaults stay put
        asset.Serialize(*ctx->serializer);
        CHECK(ctx->serializer->IsOk()); // the regression: this failed with NotFound pre-fix
        CHECK(asset.generateMipmaps == true);
        CHECK(asset.usage == texcomp::TextureUsage::Color);
        CHECK(asset.compression == texcomp::CompressionChoice::Default);
    }

    // Current-format round-trip: the v2 scope writes AND reads the new fields.
    foundation::core::MemoryStream newBytes;
    {
        auto ctx = XmlSerializerFactory()(newBytes, foundation::core::SerializeMode::Write);
        pipeline::TextureAsset asset;
        asset.usage = texcomp::TextureUsage::Normal;
        asset.compression = texcomp::CompressionChoice::Quality;
        foundation::core::BeginVersionedPayload(*ctx->serializer,
                                                pipeline::TextureAsset::StaticType());
        asset.Serialize(*ctx->serializer);
        foundation::core::EndVersionedPayload(*ctx->serializer);
        REQUIRE(ctx->serializer->IsOk());
        ctx->Flush(newBytes);
    }
    {
        (void)newBytes.Seek(0, foundation::core::SeekOrigin::Begin);
        auto ctx = XmlSerializerFactory()(newBytes, foundation::core::SerializeMode::Read);
        pipeline::TextureAsset asset;
        foundation::core::BeginVersionedPayload(*ctx->serializer,
                                                pipeline::TextureAsset::StaticType());
        asset.Serialize(*ctx->serializer);
        foundation::core::EndVersionedPayload(*ctx->serializer);
        CHECK(ctx->serializer->IsOk());
        CHECK(asset.usage == texcomp::TextureUsage::Normal);
        CHECK(asset.compression == texcomp::CompressionChoice::Quality);
    }
}

TEST_CASE("texture import: usage inference from texture-pack name tokens")
{
    using pipeline::InferTextureUsage;
    // The universal suffixes, matched mid-stem with the boundary rule.
    CHECK(InferTextureUsage(u8"foo_nor_gl_4k") == texcomp::TextureUsage::Normal);
    CHECK(InferTextureUsage(u8"brick_NORMAL") == texcomp::TextureUsage::Normal);
    CHECK(InferTextureUsage(u8"bar_disp_2k") == texcomp::TextureUsage::Mask);
    CHECK(InferTextureUsage(u8"ground_rough_1k") == texcomp::TextureUsage::Mask);
    CHECK(InferTextureUsage(u8"kit_orm") == texcomp::TextureUsage::Mask);
    CHECK(InferTextureUsage(u8"trim_AO_2k") == texcomp::TextureUsage::Mask);
    // Everything else keeps the Color default - including boundary-rule near-misses.
    CHECK(InferTextureUsage(u8"grass_diff") == texcomp::TextureUsage::Color);
    CHECK(InferTextureUsage(u8"my_armor") == texcomp::TextureUsage::Color);   // "_arm" + letter
    CHECK(InferTextureUsage(u8"town_north") == texcomp::TextureUsage::Color); // "_nor" + letter
}

TEST_CASE("texture profiles configure usage + color space with the sampler")
{
    pipeline::TextureAsset a;
    a.SetupForNormalMap();
    CHECK(a.usage == texcomp::TextureUsage::Normal);
    CHECK(a.colorSpace == image::ImageColorSpace::Linear);
    CHECK(a.generateMipmaps);                                  // rides the 3D sampler
    CHECK(a.wrapU == TextureWrap::Repeat);

    a.SetupForDataMask();
    CHECK(a.usage == texcomp::TextureUsage::Mask);
    CHECK(a.colorSpace == image::ImageColorSpace::Linear);

    a.SetupForUI();
    CHECK(a.usage == texcomp::TextureUsage::Color);
    CHECK(a.colorSpace == image::ImageColorSpace::Srgb);
    CHECK(!a.generateMipmaps);

    a.SetupForEquirectangularSkybox();
    CHECK(a.usage == texcomp::TextureUsage::HDR);
    CHECK(a.colorSpace == image::ImageColorSpace::Linear);

    a.SetupForCubemapSkybox();
    CHECK(a.usage == texcomp::TextureUsage::HDR);
    CHECK(a.colorSpace == image::ImageColorSpace::Linear);
    CHECK(a.shape == TextureShape::Cubemap);
}

TEST_CASE("texture-import: DescribeImport lists one asset; a selection rename renames it")
{
    RegisterTextureAsset();

    const StringView dir = u8"scratch_tex_rename_project";
    auto cleanTree = [&]()
    {
        FileDelete(PathJoin(dir, u8"Project.xml"));
        FileDelete(PathJoin(dir, u8"Sources/wall.png"));
        FileDelete(PathJoin(dir, u8"Content/wall_albedo.xasset"));
        for (StringView sub : {u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache"})
        {
            RemoveDirectory(PathJoin(dir, sub));
        }
        RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(editor::EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<editor::EditorProject> project = editor::EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    const byte fakePng[6] = {byte{'P'}, byte{'N'}, byte{'G'}, byte{4}, byte{5}, byte{6}};
    REQUIRE(WriteFile(u8"wall.png", Span<const byte>(fakePng, 6)).IsOk());

    TextureFileImporter importer;
    pipeline::ImportPlan plan = importer.DescribeImport(u8"wall.png", nullptr, nullptr);
    REQUIRE(plan.entries.Size() == 1u);
    CHECK(plan.entries[0].kind == pipeline::ImportResourceKind::Asset);
    CHECK(plan.entries[0].sourceName == u8"wall");
    CHECK(plan.entries[0].targetName == u8"wall");

    // The review dialog's rename travels on the BASE options (option-less importer).
    auto options = MakeRef<pipeline::ImportOptions>(DefaultAllocator());
    plan.entries[0].targetName = String(u8"wall_albedo");
    options->selection = Move(plan);
    Result<foundation::content::Instance*> imported = importer.Import(
        u8"wall.png", pipeline::ImportContext{project->SourcesRoot()},
        *project->SourceDb().RootGroup(), options.Get(), nullptr, nullptr);
    REQUIRE(imported.HasValue());
    CHECK(imported.Value()->Name() == StringView(u8"wall_albedo"));
    CHECK(project->SourceDb().RootGroup()->GetInstance(u8"wall") == nullptr);
    // The SOURCE keeps its file identity - only the asset instance is renamed.
    CHECK(FileExists(PathJoin(dir, u8"Sources/wall.png").AsView()));

    // RE-IMPORT MEMORY: the renamed asset is found again through its typed fileName
    // back-reference; merging pre-seeds the fresh plan with the rename, and committing
    // with it UPDATES the renamed instance instead of minting a duplicate "wall".
    pipeline::ImportPlan stored =
        importer.StoredSelection(*project->SourceDb().RootGroup(), u8"wall.png");
    REQUIRE(stored.entries.Size() == 1u);
    CHECK(stored.entries[0].targetName == u8"wall_albedo");
    pipeline::ImportPlan fresh = importer.DescribeImport(u8"wall.png", nullptr, nullptr);
    pipeline::MergeStoredSelection(fresh, stored);
    CHECK(fresh.entries[0].targetName == u8"wall_albedo");
    auto reOptions = MakeRef<pipeline::ImportOptions>(DefaultAllocator());
    reOptions->selection = Move(fresh);
    Result<foundation::content::Instance*> reimported = importer.Import(
        u8"wall.png", pipeline::ImportContext{project->SourcesRoot()},
        *project->SourceDb().RootGroup(), reOptions.Get(), nullptr, nullptr);
    REQUIRE(reimported.HasValue());
    CHECK(reimported.Value() == imported.Value()); // same instance, updated
    CHECK(project->SourceDb().RootGroup()->GetInstance(u8"wall") == nullptr);

    FileDelete(u8"wall.png");
    cleanTree();
}
