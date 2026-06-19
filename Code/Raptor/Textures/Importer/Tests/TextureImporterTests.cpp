// Tests for raptor.textures.importer: save known images, import them back,
// verify metadata + pixels. (PNG round-trips RGBA8 losslessly.)
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import raptor.core;
import raptor.image;
import raptor.image.io;
import raptor.textures;
import raptor.textures.resource;
import raptor.textures.importer;

using namespace raptor::core;
using namespace raptor::textures;
namespace img = raptor::image;
namespace iio = raptor::image::io;

namespace
{
    // A w*h RGBA8 image whose byte[i] = (start + i) & 0xFF.
    img::Image MakeImage(u32 w, u32 h, u8 start)
    {
        img::Image image(w, h, img::PixelFormat::RGBA8);
        Span<u8> px = image.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i) { px.Data()[i] = static_cast<u8>(start + i); }
        return image;
    }
    void SavePng(const img::Image& image, StringView path)
    {
        REQUIRE(iio::SaveImage(image, path, iio::ImageFileFormat::PNG).IsOk());
    }
}

TEST_CASE("textures.importer: Import2D round-trips metadata + pixels")
{
    const StringView path = u8"raptor_teximport_2d.png";
    img::Image src = MakeImage(2, 2, 10);
    SavePng(src, path);

    TextureResource res;
    Array<u8> pixels;
    REQUIRE(TextureImporter::Import2D(path, img::ImageColorSpace::Srgb, res, pixels).IsOk());

    CHECK(res.shape == TextureShape::Texture2D);
    CHECK(res.imageWidth == 2u);
    CHECK(res.imageHeight == 2u);
    CHECK(res.imageFormat == img::PixelFormat::RGBA8);
    CHECK(res.colorSpace == img::ImageColorSpace::Srgb);
    CHECK(res.generateMipmaps);                // 3D preset
    CHECK(res.minFilter == TextureFilter::MipmapLinear);

    REQUIRE(pixels.Size() == 2u * 2u * 4u);
    bool match = true;
    for (usize i = 0; i < pixels.Size(); ++i) { if (pixels[i] != static_cast<u8>(10 + i)) { match = false; break; } }
    CHECK(match);

    FileDelete(path);
}

TEST_CASE("textures.importer: Import2D fails on a missing file")
{
    TextureResource res;
    Array<u8> pixels;
    CHECK_FALSE(TextureImporter::Import2D(u8"does_not_exist_xyz.png", img::ImageColorSpace::Srgb, res, pixels).IsOk());
}

TEST_CASE("textures.importer: cubemap packs 6 square faces vertically")
{
    const StringView faces[6] = {
        u8"raptor_teximport_f0.png", u8"raptor_teximport_f1.png", u8"raptor_teximport_f2.png",
        u8"raptor_teximport_f3.png", u8"raptor_teximport_f4.png", u8"raptor_teximport_f5.png",
    };
    for (int i = 0; i < 6; ++i) { SavePng(MakeImage(2, 2, static_cast<u8>(i * 16)), faces[i]); }

    TextureResource res;
    Array<u8> pixels;
    REQUIRE(TextureImporter::ImportCubemap(faces, img::ImageColorSpace::Linear, res, pixels).IsOk());

    CHECK(res.shape == TextureShape::Cubemap);
    CHECK(res.imageWidth == 2u);          // face size
    CHECK(res.imageHeight == 12u);        // 6 faces * 2
    CHECK_FALSE(res.generateMipmaps);     // skybox preset
    CHECK(res.colorSpace == img::ImageColorSpace::Linear);

    const usize faceBytes = 2u * 2u * 4u;
    REQUIRE(pixels.Size() == faceBytes * 6);
    // Face i begins with its start byte (i*16).
    CHECK(pixels[0] == 0u);
    CHECK(pixels[faceBytes] == 16u);
    CHECK(pixels[faceBytes * 5] == static_cast<u8>(5 * 16));

    for (int i = 0; i < 6; ++i) { FileDelete(faces[i]); }
}

TEST_CASE("textures.importer: cubemap rejects non-square / mismatched faces")
{
    const StringView faces[6] = {
        u8"raptor_teximport_b0.png", u8"raptor_teximport_b1.png", u8"raptor_teximport_b2.png",
        u8"raptor_teximport_b3.png", u8"raptor_teximport_b4.png", u8"raptor_teximport_b5.png",
    };
    SavePng(MakeImage(2, 3, 0), faces[0]);                 // non-square first face
    for (int i = 1; i < 6; ++i) { SavePng(MakeImage(2, 2, 0), faces[i]); }

    TextureResource res;
    Array<u8> pixels;
    CHECK_FALSE(TextureImporter::ImportCubemap(faces, img::ImageColorSpace::Linear, res, pixels).IsOk());

    for (int i = 0; i < 6; ++i) { FileDelete(faces[i]); }
}

TEST_CASE("textures.importer: DetectCubemapFaces finds a face set")
{
    const StringView present[6] = {
        u8"raptor_sky_px.png", u8"raptor_sky_nx.png", u8"raptor_sky_py.png",
        u8"raptor_sky_ny.png", u8"raptor_sky_pz.png", u8"raptor_sky_nz.png",
    };
    for (int i = 0; i < 6; ++i) { SavePng(MakeImage(1, 1, 0), present[i]); }

    String out[6];
    REQUIRE(TextureImporter::DetectCubemapFaces(u8"raptor_sky_px.png", out).IsOk());
    CHECK(out[0] == StringView(u8"raptor_sky_px.png"));
    CHECK(out[1] == StringView(u8"raptor_sky_nx.png"));
    CHECK(out[5] == StringView(u8"raptor_sky_nz.png"));

    // A non-cube name resolves nothing.
    String none[6];
    CHECK_FALSE(TextureImporter::DetectCubemapFaces(u8"raptor_random_image.png", none).IsOk());

    for (int i = 0; i < 6; ++i) { FileDelete(present[i]); }
}
