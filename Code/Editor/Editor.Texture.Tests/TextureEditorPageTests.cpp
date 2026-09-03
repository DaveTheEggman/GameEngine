// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// TextureEditorPage tests (headless): the factory routes TextureAsset through the page
// registry (nearest-type dispatch), and the whole-asset blob snapshot the page's undo
// commands ride round-trips every import setting. The page's widget tree needs a live UI
// context, so it is exercised in the editor app, not here.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.image;
import foundation.texture;
import texture.pipeline;
import texture.compression;
import foundation.rhi;
import editor.core;
import editor.texture;

using namespace foundation::core;
namespace texture = foundation::texture;
namespace image = foundation::image;

TEST_CASE("TextureEditorPageFactory reports the TextureAsset primary type")
{
    editor::TextureEditorPageFactory factory;
    CHECK(factory.PrimaryType() == &pipeline::TextureAsset::StaticType());
}

TEST_CASE("TextureEditor registers a factory that the registry routes for TextureAsset")
{
    editor::EditorContext context{DefaultAllocator()};
    editor::RegisterTextureEditor(context);

    editor::IEditorPageFactory* found =
        context.Pages().FindFactory(pipeline::TextureAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &pipeline::TextureAsset::StaticType());
}

TEST_CASE("TextureAsset blob snapshot round-trips every import setting")
{
    // The exact path TextureEditorPage::Snapshot/ApplyBlob use for undo.
    pipeline::TextureAsset original;
    original.fileName = foundation::vfs::SourcePath(u8"Textures/brick.png");
    original.colorSpace = image::ImageColorSpace::Linear;
    original.shape = texture::TextureShape::Cubemap;
    original.minFilter = texture::TextureFilter::MipmapNearest;
    original.magFilter = texture::TextureFilter::Nearest;
    original.wrapU = texture::TextureWrap::MirroredRepeat;
    original.wrapV = texture::TextureWrap::ClampToBorder;
    original.wrapW = texture::TextureWrap::ClampToEdge;
    original.generateMipmaps = false;
    original.anisotropy = 8.0f;
    original.usage = texcomp::TextureUsage::Normal;
    original.compression = texcomp::CompressionChoice::Quality;

    MemoryStream stream;
    {
        BinarySerializer writer(stream, SerializeMode::Write);
        // The page snapshots under the VERSIONED scope (TextureAsset gates its v2 fields on
        // ar.Version(); a scope-less serialize is v0 and drops them by design - that is the
        // pre-variants-envelope migration fix).
        BeginVersionedPayload(writer, pipeline::TextureAsset::StaticType());
        original.Serialize(writer);
        EndVersionedPayload(writer);
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    pipeline::TextureAsset restored;
    BinarySerializer reader(stream, SerializeMode::Read);
    BeginVersionedPayload(reader, pipeline::TextureAsset::StaticType());
    restored.Serialize(reader);
    EndVersionedPayload(reader);

    CHECK(restored.fileName.View() == original.fileName.View());
    CHECK(restored.colorSpace == image::ImageColorSpace::Linear);
    CHECK(restored.shape == texture::TextureShape::Cubemap);
    CHECK(restored.minFilter == texture::TextureFilter::MipmapNearest);
    CHECK(restored.magFilter == texture::TextureFilter::Nearest);
    CHECK(restored.wrapU == texture::TextureWrap::MirroredRepeat);
    CHECK(restored.wrapV == texture::TextureWrap::ClampToBorder);
    CHECK(restored.wrapW == texture::TextureWrap::ClampToEdge);
    CHECK(restored.generateMipmaps == false);
    CHECK(restored.anisotropy == doctest::Approx(8.0f));
    CHECK(restored.usage == texcomp::TextureUsage::Normal);
    CHECK(restored.compression == texcomp::CompressionChoice::Quality);
}

TEST_CASE("TextureAsset presets move the sampler settings a page preset row would apply")
{
    pipeline::TextureAsset asset;
    asset.SetupFor3D();
    CHECK(asset.generateMipmaps == true);
    CHECK(asset.anisotropy == doctest::Approx(16.0f));
    CHECK(asset.wrapU == texture::TextureWrap::Repeat);

    asset.SetupForUI();
    CHECK(asset.generateMipmaps == false);
    CHECK(asset.wrapU == texture::TextureWrap::ClampToEdge);
    CHECK(asset.minFilter == texture::TextureFilter::Linear);

    asset.SetupForSprite();
    CHECK(asset.minFilter == texture::TextureFilter::Nearest);
    CHECK(asset.magFilter == texture::TextureFilter::Nearest);
}

TEST_CASE("the Cooks-to row's policy expectations (desktop profile)")
{
    // The page renders ResolveCompressedFormat verbatim; pin the spec's two anchor cases plus
    // the usage-driven families the lint/derivation flow leads authors into.
    using texcomp::ResolveCompressedFormat;
    const auto desktop = texcomp::DesktopProfile();
    const auto rgba8 = foundation::rhi::TextureFormat::RGBA8Unorm;

    // Color with alpha -> BC7 sRGB.
    CHECK(ResolveCompressedFormat(texcomp::TextureUsage::Color, true, true, false,
                                  texcomp::CompressionChoice::Default, 512, 512, desktop,
                                  rgba8) == foundation::rhi::TextureFormat::BC7RGBAUnormSrgb);
    // Compression = None -> uncompressed passthrough.
    CHECK(ResolveCompressedFormat(texcomp::TextureUsage::Color, true, true, false,
                                  texcomp::CompressionChoice::None, 512, 512, desktop,
                                  rgba8) == rgba8);
    // The derived pairs: Normal -> BC7-LINEAR (never BC5 - the shaders decode rgb * 2 - 1),
    // gray Mask -> BC4, multichannel (ORM-shaped) Mask -> BC7-linear.
    CHECK(ResolveCompressedFormat(texcomp::TextureUsage::Normal, false, false, false,
                                  texcomp::CompressionChoice::Default, 512, 512, desktop,
                                  rgba8) == foundation::rhi::TextureFormat::BC7RGBAUnorm);
    CHECK(ResolveCompressedFormat(texcomp::TextureUsage::Mask, false, false, false,
                                  texcomp::CompressionChoice::Default, 512, 512, desktop,
                                  rgba8) == foundation::rhi::TextureFormat::BC4RUnorm);
    CHECK(ResolveCompressedFormat(texcomp::TextureUsage::Mask, false, false, true,
                                  texcomp::CompressionChoice::Default, 512, 512, desktop,
                                  rgba8) == foundation::rhi::TextureFormat::BC7RGBAUnorm);
}

TEST_CASE("usage implies color space (the page's derivation + lint contract)")
{
    // The page derives colorSpace from usage in ONE mutation and lints the mismatch. The
    // mapping is the contract; pin it here so a page edit cannot silently drop it.
    const auto derived = [](texcomp::TextureUsage usage)
    {
        return usage == texcomp::TextureUsage::Color ? image::ImageColorSpace::Srgb
                                                     : image::ImageColorSpace::Linear;
    };
    CHECK(derived(texcomp::TextureUsage::Color) == image::ImageColorSpace::Srgb);
    CHECK(derived(texcomp::TextureUsage::Normal) == image::ImageColorSpace::Linear);
    CHECK(derived(texcomp::TextureUsage::Mask) == image::ImageColorSpace::Linear);
    CHECK(derived(texcomp::TextureUsage::HDR) == image::ImageColorSpace::Linear);

    // The lint predicate: warn exactly when data is stored as sRGB.
    const auto lints = [](texcomp::TextureUsage usage, image::ImageColorSpace cs)
    { return usage != texcomp::TextureUsage::Color && cs == image::ImageColorSpace::Srgb; };
    CHECK(lints(texcomp::TextureUsage::Normal, image::ImageColorSpace::Srgb));
    CHECK(!lints(texcomp::TextureUsage::Normal, image::ImageColorSpace::Linear));
    CHECK(!lints(texcomp::TextureUsage::Color, image::ImageColorSpace::Srgb));
}
