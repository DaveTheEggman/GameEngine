// Raptor::TexturesResource — the `raptor.textures.resource` module.
//
// The texture as a managed resource — the first concrete user of the
// content/resource stack's source->product split:
//   * TextureResource (source): an ISerializable in the content DB carrying the
//     sampler/format metadata (full editor fidelity). Pixel bytes are a heavy
//     "pixels" data stream, not in the envelope.
//   * Texture (product): the lean runtime object a renderer consumes — owns the
//     pixel buffer + resolved RHI format + sampler state, and yields a
//     TextureData descriptor for GPU upload.
//   * TextureFactory (IResourceFactory): builds the product from the source.
//
// Mirrors Sedulous.Textures.Resources, on Raptor's resource framework.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.textures.resource;

import raptor.core;
import raptor.rhi;
import raptor.textures;
import raptor.image;
import raptor.content;
import raptor.resource;

using namespace raptor::core;
using namespace raptor::resource;

export namespace raptor::textures
{
    namespace img = raptor::image;
    namespace rhi = raptor::rhi;

    // ---- Source: serializable texture metadata (content DB) --------------
    class TextureResource final : public ISerializable
    {
        RAPTOR_OBJECT(TextureResource, ISerializable)
    public:
        // Sampler / upload metadata.
        TextureShape shape = TextureShape::Texture2D;
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapU = TextureWrap::Repeat;
        TextureWrap wrapV = TextureWrap::Repeat;
        TextureWrap wrapW = TextureWrap::Repeat;
        bool generateMipmaps = true;
        f32 anisotropy = 1.0f;

        // Image description (pixels live in the "pixels" data stream).
        u32 imageWidth = 0;
        u32 imageHeight = 0;
        img::PixelFormat imageFormat = img::PixelFormat::RGBA8;
        img::ImageColorSpace colorSpace = img::ImageColorSpace::Srgb;

        void Serialize(ISerializer& ar) override
        {
            raptor::core::Serialize(ar, "shape", shape);
            raptor::core::Serialize(ar, "minFilter", minFilter);
            raptor::core::Serialize(ar, "magFilter", magFilter);
            raptor::core::Serialize(ar, "wrapU", wrapU);
            raptor::core::Serialize(ar, "wrapV", wrapV);
            raptor::core::Serialize(ar, "wrapW", wrapW);
            raptor::core::Serialize(ar, "generateMipmaps", generateMipmaps);
            raptor::core::Serialize(ar, "anisotropy", anisotropy);
            raptor::core::Serialize(ar, "imageWidth", imageWidth);
            raptor::core::Serialize(ar, "imageHeight", imageHeight);
            raptor::core::Serialize(ar, "imageFormat", imageFormat);
            raptor::core::Serialize(ar, "colorSpace", colorSpace);
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
    };

    // ---- Product: lean runtime texture ----------------------------------
    class Texture final : public Object
    {
        RAPTOR_OBJECT(Texture, Object)
    public:
        u32 width = 0;
        u32 height = 0;
        rhi::TextureFormat format = rhi::TextureFormat::RGBA8Unorm;
        rhi::TextureDimension dimension = rhi::TextureDimension::Texture2D;
        u32 mipLevels = 1;

        // Resolved sampler state.
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapU = TextureWrap::Repeat;
        TextureWrap wrapV = TextureWrap::Repeat;
        TextureWrap wrapW = TextureWrap::Repeat;
        bool generateMipmaps = true;
        f32 anisotropy = 1.0f;

        [[nodiscard]] Span<const u8> Pixels() const noexcept { return Span<const u8>(m_pixels.Data(), m_pixels.Size()); }
        void SetPixels(Array<u8>&& pixels) noexcept { m_pixels = Move(pixels); }

        // A GPU-upload descriptor referencing this texture's owned pixel buffer.
        [[nodiscard]] TextureData Descriptor() const
        {
            TextureData d = TextureData::Create2D(m_pixels.Data(), static_cast<u64>(m_pixels.Size()), width, height, format);
            d.mipLevels = mipLevels;
            d.dimension = dimension;
            return d;
        }

    private:
        Array<u8> m_pixels; // owned CPU pixels, ready for GPU upload
    };

    // ---- Factory: source -> product -------------------------------------
    class TextureFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &Texture::StaticType(); }

        [[nodiscard]] RefPtr<Object> Create(raptor::content::Instance& instance) override
        {
            RefPtr<ISerializable> source = instance.ReadObject();
            TextureResource* res = Cast<TextureResource>(source.Get());
            if (res == nullptr) { return RefPtr<Object>{}; }

            RefPtr<Texture> texture = MakeRef<Texture>(DefaultAllocator());
            texture->width = res->imageWidth;
            texture->height = res->imageHeight;
            texture->format = TextureFormatUtils::Convert(res->imageFormat, res->colorSpace);
            texture->dimension = rhi::TextureDimension::Texture2D;
            texture->mipLevels = 1;
            texture->minFilter = res->minFilter;
            texture->magFilter = res->magFilter;
            texture->wrapU = res->wrapU;
            texture->wrapV = res->wrapV;
            texture->wrapW = res->wrapW;
            texture->generateMipmaps = res->generateMipmaps;
            texture->anisotropy = res->anisotropy;

            // Pixels come from the heavy "pixels" data stream (may be absent for
            // a metadata-only resource).
            Array<u8> pixels;
            if (UniquePtr<IStream> stream = instance.ReadData(u8"pixels"))
            {
                const i64 size = stream->Size();
                if (size > 0)
                {
                    pixels.Resize(static_cast<usize>(size));
                    const u64 read = stream->Read(pixels.Data(), static_cast<u64>(size));
                    if (read != static_cast<u64>(size)) { pixels.Clear(); }
                }
            }
            texture->SetPixels(Move(pixels));
            return texture;
        }
    };

    // Registers TextureResource for content-DB construction + deserialization.
    inline void RegisterTextureResource()
    {
        GlobalTypeRegistry().Register(TextureResource::StaticType());
        RegisterSerializable<TextureResource>();
    }
}

export namespace raptor::textures
{
    // Reflection definitions (object identity for the source + product types).
    RAPTOR_DEFINE_OBJECT(TextureResource, "raptor::textures")
    RAPTOR_DEFINE_OBJECT(Texture, "raptor::textures")
}
