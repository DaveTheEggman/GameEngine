// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Full font asset pipeline: author a FontAsset over a real TTF -> cook with
// FontAssetBuilder into an output content DB -> load the cooked FontResource through the
// ResourceManager with the device-free FontFactory and verify the rasterizer-free product.
// Covers both bake modes (coverage size ramp + msdfgen MSDF) and the importer's file claim.
#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import pipeline.core;
import editor.core;
import pipeline.importer;
import foundation.fonts;
import foundation.image;
import foundation.fonts.resource;
import fonts.pipeline;
import foundation.xml.serialization; // XmlSerializerFactory: the legacy-payload test rewrites a keyed file

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::fonts;

namespace
{
    constexpr StringView kSourceFont = u8"scratch_fontpipe_src.ttf";

    void RemoveTree()
    {
        FileDelete(kSourceFont);
        FileDelete(u8"scratch_fontpipe_out_db/uifont.rasset");
        FileDelete(u8"scratch_fontpipe_out_db/uifont.data.bin");
        RemoveDirectory(u8"scratch_fontpipe_out_db");
    }

    // Copy the repo's DejaVu Mono beside the test (the sources mount is the CWD).
    bool StageSourceFont()
    {
        return FileCopyPreserving(
            StringView(reinterpret_cast<const utf8char*>(TEST_FONT_PATH)), kSourceFont);
    }
}

TEST_CASE("font.pipeline: FontAsset raster ramp -> cook -> rasterizer-free Font")
{
    RegisterFontAsset();
    RemoveTree();
    REQUIRE(StageSourceFont());

    NativeFileSystem outMount(u8"scratch_fontpipe_out_db", DefaultAllocator());
    Guid id;
    {
        foundation::content::ContentDatabase outDb(foundation::core::DefaultAllocator(), 
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"uifont", FontResource::StaticType());
        id = inst->Id();

        FontAsset asset;
        asset.fileName = foundation::vfs::SourcePath(kSourceFont);
        asset.family = String(u8"TestMono");
        asset.sizes.Clear();
        asset.sizes.PushBack(12.0f);
        asset.sizes.PushBack(24.0f);
        asset.firstCodepoint = 32;
        asset.lastCodepoint = 126; // ASCII keeps the bake fast
        asset.atlasWidth = 512;
        asset.atlasHeight = 512;

        FontAssetBuilder builder;
        REQUIRE(builder.AssetType() == &FontAsset::StaticType());
        REQUIRE(builder.ProductType() == &FontResource::StaticType());
        NativeFileSystem srcMount(u8".", DefaultAllocator());
        pipeline::AssetBuildContext ctx{DefaultAllocator()};
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    foundation::content::ContentDatabase outDb(foundation::core::DefaultAllocator(), outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    FontFactory factory(DefaultAllocator());
    ResourceManager manager(DefaultAllocator(), outDb);
    manager.AddFactory(&factory);

    Proxy<Font> font = manager.Bind<Font>(id);
    REQUIRE(font);
    CHECK(font->Family() == u8"TestMono");
    REQUIRE(font->EntryCount() == 2u);

    const Font::Entry* entry = font->ClosestEntry(12.0f);
    REQUIRE(entry != nullptr);
    CHECK(entry->pixelHeight == doctest::Approx(12.0f));
    REQUIRE(entry->font);
    CHECK(entry->font->HasGlyph('A'));
    CHECK(entry->font->GetGlyphInfo('A').advanceWidth > 0.0f);
    CHECK(entry->font->Metrics().ascent > 0.0f);
    REQUIRE(entry->atlas);
    CHECK(entry->atlas->Mode() == AtlasMode::Coverage);
    CHECK(entry->atlas->Contains('A'));
    GlyphQuad quad;
    f32 cursor = 0.0f;
    CHECK(entry->atlas->GetGlyphQuad('A', cursor, 0.0f, quad));
    CHECK(cursor > 0.0f); // the advance moved
    REQUIRE(entry->atlasImage);
    CHECK(entry->atlasImage->Width() == 512u);
    CHECK(entry->atlasImage->PixelData().Size() == 512u * 512u * 4u); // RGBA expansion

    // A monospace face measures a string as advance * count.
    const f32 one = entry->font->GetGlyphInfo('M').advanceWidth;
    CHECK(entry->font->MeasureString(u8"MM") == doctest::Approx(one * 2.0f).epsilon(0.01));

    RemoveTree();
}

TEST_CASE("font.pipeline: MSDF bake cooks a DistanceField resource")
{
    RegisterFontAsset();
    RemoveTree();
    REQUIRE(StageSourceFont());

    NativeFileSystem outMount(u8"scratch_fontpipe_out_db", DefaultAllocator());
    Guid id;
    {
        foundation::content::ContentDatabase outDb(foundation::core::DefaultAllocator(), 
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"uifont", FontResource::StaticType());
        id = inst->Id();

        FontAsset asset;
        asset.fileName = foundation::vfs::SourcePath(kSourceFont);
        asset.mode = FontBakeMode::DistanceField;
        asset.distanceFieldSize = 32.0f;
        asset.firstCodepoint = 'A';
        asset.lastCodepoint = 'Z'; // a small range keeps msdfgen quick
        asset.atlasWidth = 256;
        asset.atlasHeight = 256;

        FontAssetBuilder builder;
        NativeFileSystem srcMount(u8".", DefaultAllocator());
        pipeline::AssetBuildContext ctx{DefaultAllocator()};
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    foundation::content::ContentDatabase outDb(foundation::core::DefaultAllocator(), outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    FontFactory factory(DefaultAllocator());
    ResourceManager manager(DefaultAllocator(), outDb);
    manager.AddFactory(&factory);

    Proxy<Font> font = manager.Bind<Font>(id);
    REQUIRE(font);
    REQUIRE(font->EntryCount() == 1u);
    const Font::Entry& entry = font->EntryAt(0);
    REQUIRE(entry.atlas);
    CHECK(entry.atlas->Mode() == AtlasMode::DistanceField);
    CHECK(entry.atlas->DistanceFieldRange() > 0.0f);
    CHECK(entry.atlas->Contains('Q'));
    REQUIRE(entry.atlasImage);
    CHECK(entry.atlasImage->ColorSpace() == foundation::image::ImageColorSpace::Linear);

    RemoveTree();
}

TEST_CASE("font.importer: claims font extensions only")
{
    FontAssetImporter importer;
    CHECK(importer.Accepts(u8"ttf"));
    CHECK(importer.Accepts(u8"otf"));
    CHECK(importer.Accepts(u8"ttc"));
    CHECK_FALSE(importer.Accepts(u8"png"));
    CHECK_FALSE(importer.Accepts(u8"xasset"));
}

TEST_CASE("font.pipeline: builder fails on a missing source file")
{
    RegisterFontAsset();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_fontpipe_out_db", DefaultAllocator());
    foundation::content::ContentDatabase outDb(foundation::core::DefaultAllocator(), outMount, foundation::core::BinarySerializerFactory(),
                                             u8".rasset");
    auto* inst = outDb.RootGroup()->CreateInstance(u8"uifont", FontResource::StaticType());

    FontAsset asset;
    asset.fileName = foundation::vfs::SourcePath(u8"does_not_exist_xyz.ttf");
    FontAssetBuilder builder;
    NativeFileSystem srcMount(u8".", DefaultAllocator());
    pipeline::AssetBuildContext ctx{DefaultAllocator()};
    ctx.sources = &srcMount;
    ctx.output = inst;
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());
    RemoveTree();
}

TEST_CASE("font.pipeline: v0 FontAsset payloads (abbreviated dfSize key) migrate on read")
{
    // 2026-09-06: FontAsset's distance-field size key went from "dfSize" to
    // "distanceFieldSize" under DataVersion 1. Every project saved before that holds the v0
    // form, and a keyed reader fails on a missing key - so Serialize branches on the payload
    // version. Prove it against a real v0 file: write the current form, rewrite it into what
    // an old project contains, read it back.
    RegisterFontAsset();
    RegisterSerializable<FontAsset>();
    (void)RemoveDirectoryRecursive(u8"scratch_fontpipe_legacy_db");
    NativeFileSystem mount(u8"scratch_fontpipe_legacy_db", DefaultAllocator());
    Guid id;
    {
        foundation::content::ContentDatabase db(DefaultAllocator(), mount,
                                                foundation::xml::XmlSerializerFactory(),
                                                u8".xasset");
        auto* inst = db.RootGroup()->CreateInstance(u8"legacyfont", FontAsset::StaticType());
        id = inst->Id();
        FontAsset asset;
        asset.mode = FontBakeMode::DistanceField;
        asset.distanceFieldSize = 37.0f;
        REQUIRE(inst->WriteObject(asset).IsOk());
    }

    const StringView path = u8"scratch_fontpipe_legacy_db/legacyfont.xasset";
    auto bytes = ReadFile(path, DefaultAllocator());
    REQUIRE(bytes.HasValue());
    std::string text(reinterpret_cast<const char*>(bytes.Value().Data()), bytes.Value().Size());
    // The current writer: DataVersion 1 + the full key.
    CHECK(text.find("name=\"distanceFieldSize\"") != std::string::npos);
    CHECK(text.find("name=\"dfSize\"") == std::string::npos);

    // Rewrite into the v0 form: version 0 in the dataVersions chain + the abbreviated key.
    const std::size_t chain = text.find("name=\"dataVersions\"");
    REQUIRE(chain != std::string::npos);
    const std::size_t version = text.find("<u32 name=\"version\">1</u32>", chain);
    REQUIRE(version != std::string::npos);
    text.replace(version, std::strlen("<u32 name=\"version\">1</u32>"), "<u32 name=\"version\">0</u32>");
    const std::size_t key = text.find("name=\"distanceFieldSize\"");
    text.replace(key, std::strlen("name=\"distanceFieldSize\""), "name=\"dfSize\"");
    REQUIRE(WriteFile(path, Span<const byte>{reinterpret_cast<const byte*>(text.data()),
                                             text.size()})
                .IsOk());

    {
        foundation::content::ContentDatabase db(DefaultAllocator(), mount,
                                                foundation::xml::XmlSerializerFactory(),
                                                u8".xasset");
        foundation::content::Instance* inst = db.GetInstance(id);
        REQUIRE(inst != nullptr);
        RefPtr<ISerializable> object = inst->ReadObject();
        auto* asset = Cast<FontAsset>(object.Get());
        REQUIRE(asset != nullptr);
        CHECK(asset->mode == FontBakeMode::DistanceField);
        CHECK(asset->distanceFieldSize == doctest::Approx(37.0f)); // read through the v0 key
    }
    (void)RemoveDirectoryRecursive(u8"scratch_fontpipe_legacy_db");
}
