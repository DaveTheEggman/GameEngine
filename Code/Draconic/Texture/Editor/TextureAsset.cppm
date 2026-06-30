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
// Never linked by the runtime. Cubemap import is deferred.

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
    namespace img = draconic::image;

    // Source asset: an image file + how it should become a GPU texture.
    class TextureAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(TextureAsset, draconic::editor::Asset)
    public:
        img::ImageColorSpace colorSpace = img::ImageColorSpace::Srgb;
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
            colorSpace = img::ImageColorSpace::Linear;
            shape = TextureShape::Texture2D; minFilter = TextureFilter::Linear; magFilter = TextureFilter::Linear;
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
            img::Image image;
            const Status loaded = img::io::LoadImage(path, image);
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
        static void Import2D(StringView path, img::ImageColorSpace colorSpace, TextureAsset& outAsset)
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
    };

    // Registers TextureAsset for content-DB construction + deserialization.
    inline void RegisterTextureAsset()
    {
        GlobalTypeRegistry().Register(TextureAsset::StaticType());
        RegisterSerializable<TextureAsset>();
    }

    DRACONIC_DEFINE_OBJECT(TextureAsset, "draconic::texture")
}
