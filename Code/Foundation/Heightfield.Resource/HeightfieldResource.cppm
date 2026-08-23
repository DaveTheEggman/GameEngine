/// Foundation::Heightfield.Resource - the `foundation.heightfield.resource` module.
///
/// The heightfield as a referenceable, cooked resource. HeightfieldSource is the small serialized
/// METADATA (size + world footprint + Y range); the u16 sample bulk rides a separate "heights" data
/// stream (bulk-data sidecar rule - never inline in the serialized object, the ImageResource "pixels"
/// precedent). HeightfieldFactory reads the metadata + the stream and builds the runtime Heightfield
/// (foundation.heightfield). Terrain, the physics collider, and the nav bake all resolve
/// Ref<Heightfield> to this product.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.heightfield.resource;

import foundation.core;
import foundation.resource;
import foundation.content;
import foundation.heightfield;

using namespace foundation::core;
using namespace foundation::resource;

export namespace foundation::heightfield
{
    /// The name of the sidecar stream carrying the raw u16 samples.
    inline constexpr StringView kHeightStream = u8"heights";

    /// Cooked heightfield METADATA: the grid parameters. The u16 sample bulk is NOT here - it rides
    /// the `kHeightStream` data stream (see the module doc).
    class HeightfieldSource final : public ISerializable
    {
        RTTI_OBJECT(HeightfieldSource, ISerializable)
    public:
        i32 size = 0;
        Float2 worldSize{0.0f, 0.0f};
        f32 minY = 0.0f;
        f32 maxY = 0.0f;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "size", size);
            foundation::core::Serialize(ar, "worldSize", worldSize);
            foundation::core::Serialize(ar, "minY", minY);
            foundation::core::Serialize(ar, "maxY", maxY);
        }

        /// Capture a runtime Heightfield's METADATA into this source (for cooking). The samples are
        /// written separately via HeightBlob + WriteData(kHeightStream, ...).
        static void FromHeightfield(const Heightfield& hf, HeightfieldSource& out)
        {
            out.size = hf.Size();
            out.worldSize = hf.WorldSize();
            out.minY = hf.MinY();
            out.maxY = hf.MaxY();
        }

        /// The raw sample bytes of a heightfield, to feed WriteData(kHeightStream, ...).
        [[nodiscard]] static Span<const byte> HeightBlob(const Heightfield& hf) noexcept
        {
            const Span<const Height> samples = hf.Samples();
            return Span<const byte>(reinterpret_cast<const byte*>(samples.Data()),
                                    samples.Size() * sizeof(Height));
        }

        /// Build the runtime product from this metadata + the sidecar sample bytes. Returns an empty
        /// grid if the cooked data is inconsistent (invalid size, or a blob that does not match
        /// size*size*2) rather than a malformed grid.
        [[nodiscard]] RefPtr<Heightfield> Build(Span<const byte> blob) const
        {
            if (!IsValidSize(size))
            {
                return MakeRef<Heightfield>(DefaultAllocator());
            }
            const usize expected = static_cast<usize>(size) * static_cast<usize>(size) * sizeof(Height);
            if (blob.Size() != expected)
            {
                return MakeRef<Heightfield>(DefaultAllocator());
            }
            RefPtr<Heightfield> hf =
                MakeRef<Heightfield>(DefaultAllocator(), size, worldSize, minY, maxY);
            MemCopy(hf->Samples().Data(), blob.Data(), expected);
            return hf;
        }
    };

    /// Builds a cooked heightfield (metadata object + "heights" stream) into a runtime Heightfield.
    /// Pure-CPU (no GPU state - the terrain renderer owns the height texture, cached per resource), so
    /// the whole build runs on a worker.
    class HeightfieldFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &Heightfield::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(ResourceManager&,
                                            foundation::content::Instance& instance) override
        {
            return BuildFrom(instance);
        }
        [[nodiscard]] bool SupportsAsync() const override { return true; }
        [[nodiscard]] RefPtr<Object> DecodeStage(foundation::content::Instance& instance) override
        {
            return BuildFrom(instance);
        }
        [[nodiscard]] RefPtr<Object> FinalizeStage(ResourceManager&, RefPtr<Object> decoded) override
        {
            return decoded;
        }

    private:
        [[nodiscard]] static RefPtr<Object> BuildFrom(foundation::content::Instance& instance)
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            HeightfieldSource* src = Cast<HeightfieldSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            Array<u8> blob;
            if (UniquePtr<IStream> stream = instance.ReadData(kHeightStream))
            {
                const i64 size = stream->Size();
                if (size > 0)
                {
                    blob.Resize(static_cast<usize>(size));
                    if (stream->Read(blob.Data(), static_cast<u64>(size)) != static_cast<u64>(size))
                    {
                        blob.Clear();
                    }
                }
            }
            return src->Build(Span<const byte>(reinterpret_cast<const byte*>(blob.Data()), blob.Size()));
        }
    };

    /// Register the heightfield resource types (product + cooked source) so the content DB can
    /// deserialize a cooked heightfield and the factory can produce it.
    inline void RegisterHeightfieldResourceTypes()
    {
        GlobalTypeRegistry().Register(Heightfield::StaticType());
        GlobalTypeRegistry().Register(HeightfieldSource::StaticType());
        RegisterSerializable<HeightfieldSource>();
    }

    RTTI_DEFINE_OBJECT_VERSIONED(HeightfieldSource, "rtti::heightfield", 1)
}
