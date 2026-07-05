// Draconic::TextureEditor — the `draconic.texture.editor` module (tooling).
//
// Source-side texture authoring + cook:
//   * TextureAsset (editor::Asset): references an image file + the GPU-texture
//     intent (color space, shape, sampler state). Presets mirror Sedulous's.
//   * TextureAssetBuilder (DefaultAssetBuilder): cooks a TextureAsset into a
//     runtime TextureResource — decode the file, resolve the RHI format from the
//     pixel format + color space, write the cooked record + "data" pixel stream.
//   * TextureImporter: an authoring helper that produces a TextureAsset for an
//     image file with a sensible preset (2D / equirectangular sky).
//
// Never linked by the runtime (an authoring/cook-seam helper). Cubemap import loads + validates +
// combines 6 face files; cooking a cubemap TextureResource through the builder is still deferred.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.texture.editor;

import draconic.core;
import draconic.editor;
import draconic.rhi;
import draconic.texture;
import draconic.texture.resource;
import draconic.image;
import draconic.image.io;
import draconic.content;

using namespace draconic::core;

export namespace draconic::texture
{
    namespace image = draconic::image;

    // Source asset: an image file + how it should become a GPU texture.
    class TextureAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(TextureAsset, draconic::editor::Asset)
    public:
        image::ImageColorSpace colorSpace = image::ImageColorSpace::Srgb;
        TextureShape shape = TextureShape::Texture2D;
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapU = TextureWrap::Repeat;
        TextureWrap wrapV = TextureWrap::Repeat;
        TextureWrap wrapW = TextureWrap::Repeat;
        bool generateMipmaps = true;
        f32 anisotropy = 1.0f;

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar); // fileName
            draconic::core::Serialize(ar, "colorSpace", colorSpace);
            draconic::core::Serialize(ar, "shape", shape);
            draconic::core::Serialize(ar, "minFilter", minFilter);
            draconic::core::Serialize(ar, "magFilter", magFilter);
            draconic::core::Serialize(ar, "wrapU", wrapU);
            draconic::core::Serialize(ar, "wrapV", wrapV);
            draconic::core::Serialize(ar, "wrapW", wrapW);
            draconic::core::Serialize(ar, "generateMipmaps", generateMipmaps);
            draconic::core::Serialize(ar, "anisotropy", anisotropy);
        }

        // Presets (subset of Sedulous's).
        void SetupForUI()
        {
            shape = TextureShape::Texture2D; minFilter = TextureFilter::Linear; magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge; wrapV = TextureWrap::ClampToEdge;
            generateMipmaps = false; anisotropy = 1.0f;
        }
        void SetupForSprite()
        {
            shape = TextureShape::Texture2D; minFilter = TextureFilter::Nearest; magFilter = TextureFilter::Nearest;
            wrapU = TextureWrap::ClampToEdge; wrapV = TextureWrap::ClampToEdge;
            generateMipmaps = false; anisotropy = 1.0f;
        }
        void SetupFor3D()
        {
            shape = TextureShape::Texture2D; minFilter = TextureFilter::MipmapLinear; magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::Repeat; wrapV = TextureWrap::Repeat;
            generateMipmaps = true; anisotropy = 16.0f;
        }
        void SetupForEquirectangularSkybox()
        {
            colorSpace = image::ImageColorSpace::Linear;
            shape = TextureShape::Texture2D; minFilter = TextureFilter::Linear; magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge; wrapV = TextureWrap::ClampToEdge; wrapW = TextureWrap::ClampToEdge;
            generateMipmaps = false; anisotropy = 1.0f;
        }
        void SetupForCubemapSkybox()
        {
            shape = TextureShape::Cubemap; minFilter = TextureFilter::Linear; magFilter = TextureFilter::Linear;
            wrapU = TextureWrap::ClampToEdge; wrapV = TextureWrap::ClampToEdge; wrapW = TextureWrap::ClampToEdge;
            generateMipmaps = false; anisotropy = 1.0f;
        }
    };

    // Cooks a TextureAsset -> TextureResource (decode + resolve RHI format ->
    // cooked record + "data" pixel stream).
    class TextureAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override { return &TextureAsset::StaticType(); }

        [[nodiscard]] Status Build(const draconic::editor::Asset& asset, draconic::editor::AssetBuildContext& ctx) override
        {
            const TextureAsset& ta = static_cast<const TextureAsset&>(asset); // guarded by AssetType()
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            const String path = ResolveSource(ctx, ta.fileName);
            image::Image image;
            const Status loaded = image::io::LoadImage(path, image);
            if (!loaded.IsOk()) { return loaded; }

            TextureResource resource;
            resource.width = image.Width();
            resource.height = image.Height();
            resource.depthOrArrayLayers = 1;
            resource.mipLevels = 1;
            resource.format = TextureFormatUtils::Convert(image.Format(), ta.colorSpace);
            resource.shape = ta.shape;
            resource.minFilter = ta.minFilter;
            resource.magFilter = ta.magFilter;
            resource.wrapU = ta.wrapU;
            resource.wrapV = ta.wrapV;
            resource.wrapW = ta.wrapW;
            resource.generateMipmaps = ta.generateMipmaps;
            resource.anisotropy = ta.anisotropy;

            const Status wrote = ctx.output->WriteObject(resource);
            if (!wrote.IsOk()) { return wrote; }

            const Span<const u8> px = image.PixelData();
            return ctx.output->WriteData(u8"data",
                Span<const byte>(reinterpret_cast<const byte*>(px.Data()), px.Size()));
        }
    };

    // Authoring helper: configure a TextureAsset for an image file with a preset.
    // (Asset is a non-copyable Object, so the result is filled in place.)
    class TextureImporter
    {
    public:
        // A standard 2D texture (3D preset: mips + anisotropy).
        static void Import2D(StringView path, image::ImageColorSpace colorSpace, TextureAsset& outAsset)
        {
            outAsset.fileName = String(path);
            outAsset.SetupFor3D();
            outAsset.colorSpace = colorSpace;
        }

        // An HDR equirectangular sky (linear, clamped, no mips).
        static void ImportEquirectangular(StringView path, TextureAsset& outAsset)
        {
            outAsset.fileName = String(path);
            outAsset.SetupForEquirectangularSkybox();
        }

        // A cubemap sky from 6 face files (the first is stored as the asset source).
        static void ImportCubemap(StringView firstFacePath, TextureAsset& outAsset)
        {
            outAsset.fileName = String(firstFacePath);
            outAsset.SetupForCubemapSkybox();
        }

        // Load 6 cubemap faces into one combined buffer, faces concatenated in +X,-X,+Y,-Y,+Z,-Z order
        // (the layout a 6-layer cube texture expects). All faces must be square, the same size, and the
        // same format; the caller supplies the explicit paths. `outPixels` = 6 * faceSize*faceSize*bpp.
        [[nodiscard]] static Status LoadCubemap(Span<const StringView> facePaths, Array<u8>& outPixels, u32& outFaceSize)
        {
            if (facePaths.Size() != 6) { return ErrorCode::InvalidArgument; }
            image::Image faces[6];
            u32 faceSize = 0;
            usize faceBytes = 0;
            for (usize i = 0; i < 6; ++i)
            {
                if (!image::io::LoadImage(facePaths[i], faces[i]).IsOk()) { return ErrorCode::Unknown; }
                if (faces[i].Width() != faces[i].Height()) { return ErrorCode::Unknown; }   // cube faces are square
                if (i == 0) { faceSize = faces[0].Width(); faceBytes = faces[0].PixelData().Size(); }
                else if (faces[i].Width() != faceSize || faces[i].PixelData().Size() != faceBytes ||
                         faces[i].Format() != faces[0].Format()) { return ErrorCode::Unknown; }   // all faces must match
            }
            if (faceSize == 0 || faceBytes == 0) { return ErrorCode::Unknown; }
            outPixels.Resize(faceBytes * 6u);
            for (usize i = 0; i < 6; ++i) { MemCopy(outPixels.Data() + faceBytes * i, faces[i].PixelData().Data(), faceBytes); }
            outFaceSize = faceSize;
            return Status{};
        }

        // Given ONE face path (e.g. ".../sky_px.png"), derive all 6 face paths by matching a common
        // naming convention (px/nx/..., _posx/..., right/left/...) and rebuilding the set in
        // +X,-X,+Y,-Y,+Z,-Z order. Pure string derivation (no filesystem); pair with LoadCubemap, which
        // validates the files actually load. Returns Unknown if the path matches no known convention.
        [[nodiscard]] static Status DetectCubemapFaces(StringView oneFacePath, Array<String>& outPaths)
        {
            const StringView dir  = PathParent(oneFacePath);
            const StringView stem = PathStem(oneFacePath);
            const StringView ext  = PathExtension(oneFacePath);
            static const StringView conv[5][6] = {
                { u8"px",    u8"nx",    u8"py",    u8"ny",     u8"pz",    u8"nz"    },
                { u8"_px",   u8"_nx",   u8"_py",   u8"_ny",    u8"_pz",   u8"_nz"   },
                { u8"_posx", u8"_negx", u8"_posy", u8"_negy",  u8"_posz", u8"_negz" },
                { u8"_right",u8"_left", u8"_top",  u8"_bottom",u8"_front",u8"_back" },
                { u8"right", u8"left",  u8"top",   u8"bottom", u8"front", u8"back"  },
            };
            for (const auto& c : conv)
            {
                int matched = -1;
                for (int i = 0; i < 6; ++i) { if (EndsWithCI(stem, c[i])) { matched = i; break; } }
                if (matched < 0) { continue; }
                const StringView prefix = stem.SubStr(0, stem.Size() - c[matched].Size());
                outPaths.Clear();
                for (int i = 0; i < 6; ++i)
                {
                    String name{ prefix }; name.Append(c[i]); name.Append(ext);
                    outPaths.PushBack(dir.IsEmpty() ? name : PathJoin(dir, name.AsView()));
                }
                return Status{};
            }
            return ErrorCode::Unknown;
        }

    private:
        [[nodiscard]] static bool EndsWithCI(StringView s, StringView suffix) noexcept
        {
            if (s.Size() < suffix.Size()) { return false; }
            const usize off = s.Size() - suffix.Size();
            for (usize i = 0; i < suffix.Size(); ++i)
            {
                utf8char a = s[off + i], b = suffix[i];
                if (a >= u8'A' && a <= u8'Z') { a = static_cast<utf8char>(a + 32); }
                if (b >= u8'A' && b <= u8'Z') { b = static_cast<utf8char>(b + 32); }
                if (a != b) { return false; }
            }
            return true;
        }
    };

    // Registers TextureAsset for content-DB construction + deserialization.
    inline void RegisterTextureAsset()
    {
        GlobalTypeRegistry().Register(TextureAsset::StaticType());
        RegisterSerializable<TextureAsset>();
    }

    DRACONIC_DEFINE_OBJECT(TextureAsset, "draconic::texture")
}
