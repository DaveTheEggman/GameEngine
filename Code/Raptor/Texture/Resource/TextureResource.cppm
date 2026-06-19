// Raptor::TextureResource — the `raptor.texture.resource` module (runtime).
//
// The GPU texture as a runtime resource (model A, Traktor-style):
//   * TextureResource (ISerializable): the cooked *record* loaded from the output
//     DB — dims/RHI-format/mips/shape + sampler state. Cooked pixels live in the
//     "data" stream. This is what the factory reads (not bound directly).
//   * Texture (Object): the runtime product — owns the live rhi::Texture +
//     rhi::Sampler (created from the record + uploaded "data"). What a renderer
//     binds.
//   * TextureFactory (IResourceFactory): device-backed; cooked resource -> GPU
//     texture. Needs an rhi::Device (headless tests use the Null backend).
//
// The runtime never links the editor/source side; it loads only cooked resources.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.texture.resource;

import raptor.core;
import raptor.rhi;
import raptor.texture;
import raptor.content;
import raptor.resource;

using namespace raptor::core;
using namespace raptor::resource;

export namespace raptor::texture
{
    namespace rhi = raptor::rhi;

    // Cooked texture record (output DB). Pixels are the "data" stream.
    class TextureResource final : public ISerializable
    {
        RAPTOR_OBJECT(TextureResource, ISerializable)
    public:
        u32 width = 0;
        u32 height = 0;
        u32 depthOrArrayLayers = 1;
        u32 mipLevels = 1;
        rhi::TextureFormat format = rhi::TextureFormat::RGBA8Unorm;
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
            raptor::core::Serialize(ar, "width", width);
            raptor::core::Serialize(ar, "height", height);
            raptor::core::Serialize(ar, "depthOrArrayLayers", depthOrArrayLayers);
            raptor::core::Serialize(ar, "mipLevels", mipLevels);
            raptor::core::Serialize(ar, "format", format);
            raptor::core::Serialize(ar, "shape", shape);
            raptor::core::Serialize(ar, "minFilter", minFilter);
            raptor::core::Serialize(ar, "magFilter", magFilter);
            raptor::core::Serialize(ar, "wrapU", wrapU);
            raptor::core::Serialize(ar, "wrapV", wrapV);
            raptor::core::Serialize(ar, "wrapW", wrapW);
            raptor::core::Serialize(ar, "generateMipmaps", generateMipmaps);
            raptor::core::Serialize(ar, "anisotropy", anisotropy);
        }
    };

    // Runtime product: owns the live GPU texture + sampler.
    class Texture final : public Object
    {
        RAPTOR_OBJECT(Texture, Object)
    public:
        Texture() = default;
        ~Texture() override
        {
            if (m_device != nullptr)
            {
                if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); }
                if (m_texture != nullptr) { m_device->DestroyTexture(m_texture); }
            }
        }
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        void Adopt(rhi::Device* device, rhi::Texture* texture, rhi::Sampler* sampler,
                   u32 width, u32 height, rhi::TextureFormat format) noexcept
        {
            m_device = device; m_texture = texture; m_sampler = sampler;
            m_width = width; m_height = height; m_format = format;
        }

        [[nodiscard]] rhi::Texture* GpuTexture() const noexcept { return m_texture; }
        [[nodiscard]] rhi::Sampler* Sampler() const noexcept { return m_sampler; }
        [[nodiscard]] u32 Width() const noexcept { return m_width; }
        [[nodiscard]] u32 Height() const noexcept { return m_height; }
        [[nodiscard]] rhi::TextureFormat Format() const noexcept { return m_format; }

    private:
        rhi::Device* m_device = nullptr;     // non-owning
        rhi::Texture* m_texture = nullptr;   // owned (destroyed via device)
        rhi::Sampler* m_sampler = nullptr;   // owned
        u32 m_width = 0;
        u32 m_height = 0;
        rhi::TextureFormat m_format = rhi::TextureFormat::RGBA8Unorm;
    };

    // Cooked TextureResource -> live GPU Texture (model A). Device-backed.
    class TextureFactory final : public IResourceFactory
    {
    public:
        explicit TextureFactory(rhi::Device& device) noexcept : m_device(&device) {}

        [[nodiscard]] const TypeInfo* ProductType() const override { return &Texture::StaticType(); }

        [[nodiscard]] RefPtr<Object> Create(raptor::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            TextureResource* res = Cast<TextureResource>(object.Get());
            if (res == nullptr) { return RefPtr<Object>{}; }

            // Cooked pixels (heavy "data" stream).
            Array<u8> pixels;
            if (UniquePtr<IStream> stream = instance.ReadData(u8"data"))
            {
                const i64 size = stream->Size();
                if (size > 0)
                {
                    pixels.Resize(static_cast<usize>(size));
                    if (stream->Read(pixels.Data(), static_cast<u64>(size)) != static_cast<u64>(size)) { pixels.Clear(); }
                }
            }

            rhi::TextureDesc desc{};
            desc.dimension = rhi::TextureDimension::Texture2D;
            desc.format = res->format;
            desc.width = res->width;
            desc.height = res->height;
            desc.depth = 1;
            desc.arrayLayerCount = res->depthOrArrayLayers;
            desc.mipLevelCount = res->mipLevels;
            desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;

            rhi::Texture* texture = nullptr;
            if (!m_device->CreateTexture(desc, texture).IsOk()) { return RefPtr<Object>{}; }

            // Upload mip 0 via a transfer batch.
            if (!pixels.IsEmpty())
            {
                rhi::Queue* queue = m_device->GetQueue(rhi::QueueType::Graphics, 0);
                rhi::TransferBatch* batch = nullptr;
                if (queue != nullptr && queue->CreateTransferBatch(batch).IsOk() && batch != nullptr)
                {
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = res->width * TextureData::GetBytesPerPixel(res->format);
                    layout.rowsPerImage = res->height;
                    batch->WriteTexture(texture, Span<const u8>(pixels.Data(), pixels.Size()),
                                        layout, rhi::Extent3D{ res->width, res->height, 1 });
                    (void)batch->Submit();
                    queue->DestroyTransferBatch(batch);
                }
            }

            rhi::SamplerDesc sd{};
            sd.minFilter = ToFilterMode(res->minFilter);
            sd.magFilter = ToFilterMode(res->magFilter);
            sd.mipmapFilter = (res->minFilter == TextureFilter::MipmapLinear) ? rhi::MipmapFilterMode::Linear : rhi::MipmapFilterMode::Nearest;
            sd.addressU = ToAddressMode(res->wrapU);
            sd.addressV = ToAddressMode(res->wrapV);
            sd.addressW = ToAddressMode(res->wrapW);
            sd.maxAnisotropy = static_cast<u16>(res->anisotropy < 1.0f ? 1.0f : res->anisotropy);
            rhi::Sampler* sampler = nullptr;
            (void)m_device->CreateSampler(sd, sampler);

            RefPtr<Texture> product = MakeRef<Texture>(DefaultAllocator());
            product->Adopt(m_device, texture, sampler, res->width, res->height, res->format);
            return product;
        }

    private:
        [[nodiscard]] static rhi::FilterMode ToFilterMode(TextureFilter f)
        {
            return (f == TextureFilter::Nearest || f == TextureFilter::MipmapNearest)
                ? rhi::FilterMode::Nearest : rhi::FilterMode::Linear;
        }
        [[nodiscard]] static rhi::AddressMode ToAddressMode(TextureWrap w)
        {
            switch (w)
            {
                case TextureWrap::Repeat:         return rhi::AddressMode::Repeat;
                case TextureWrap::ClampToEdge:    return rhi::AddressMode::ClampToEdge;
                case TextureWrap::ClampToBorder:  return rhi::AddressMode::ClampToBorder;
                case TextureWrap::MirroredRepeat: return rhi::AddressMode::MirrorRepeat;
            }
            return rhi::AddressMode::Repeat;
        }

        rhi::Device* m_device;
    };

    inline void RegisterTextureResource()
    {
        GlobalTypeRegistry().Register(TextureResource::StaticType());
        RegisterSerializable<TextureResource>();
    }

    RAPTOR_DEFINE_OBJECT(TextureResource, "raptor::texture")
    RAPTOR_DEFINE_OBJECT(Texture, "raptor::texture")
}
