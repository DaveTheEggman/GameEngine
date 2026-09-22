// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.image;
import foundation.image.io;
import foundation.image.dds;

using namespace foundation::core;
using namespace foundation::image;

TEST_CASE("image: procedural create + pixel access")
{
    Image img = Image::CreateSolidColor(4, 4, Color32{10, 20, 30, 255});
    CHECK(img.Width() == 4u);
    CHECK(img.Height() == 4u);
    CHECK(img.Format() == PixelFormat::RGBA8);
    const Color32 p = img.GetPixel(1, 1);
    CHECK(p.r == 10);
    CHECK(p.g == 20);
    CHECK(p.b == 30);
}

TEST_CASE("image: save PNG and reload via stb")
{
    Image img = Image::CreateCheckerboard(64);
    REQUIRE(img.Width() == 64u);

    const StringView path = u8"scratch_image_roundtrip.png"; // relative to the test CWD (portable; /tmp is POSIX-only)
    REQUIRE(io::SaveImage(img, path, io::ImageFileFormat::PNG).IsOk());

    Image loaded;
    REQUIRE(io::LoadImage(path, loaded).IsOk());
    CHECK(loaded.Width() == 64u);
    CHECK(loaded.Height() == 64u);
    CHECK(loaded.Format() == PixelFormat::RGBA8); // stb loads as RGBA8
}

TEST_CASE("image.io: a DDS buffer loads through the generic loader as its decoded level 0")
{
    // Every image consumer (thumbnails, model loaders) reads DDS without knowing it: the
    // loaders sniff the magic and hand back level 0.
    foundation::image::dds::DdsImage src;
    src.width = 1;
    src.height = 1;
    src.format = foundation::image::dds::DdsFormat::RGBA8Srgb;
    src.colorSpaceKnown = true;
    src.data.PushBack(9);
    src.data.PushBack(8);
    src.data.PushBack(7);
    src.data.PushBack(255);
    Array<u8> file;
    REQUIRE(foundation::image::dds::WriteDds(src, file).IsOk());
    Image img;
    REQUIRE(io::LoadImageFromMemory(Span<const u8>(file.Data(), file.Size()), img).IsOk());
    CHECK(img.Format() == PixelFormat::RGBA8);
    CHECK(img.ColorSpace() == ImageColorSpace::Srgb);
    const Color32 p = img.GetPixel(0, 0);
    CHECK(p.r == 9);
    CHECK(p.g == 8);
    CHECK(p.b == 7);
    // And from a path: the sniff reads the magic, then the file.
    REQUIRE(WriteFile(u8"scratch_image_io_probe.dds", Span<const byte>(reinterpret_cast<const byte*>(file.Data()), file.Size())).IsOk());
    Image fromPath;
    REQUIRE(io::LoadImage(u8"scratch_image_io_probe.dds", fromPath).IsOk());
    CHECK(fromPath.GetPixel(0, 0).r == 9);
    FileDelete(u8"scratch_image_io_probe.dds");
}
