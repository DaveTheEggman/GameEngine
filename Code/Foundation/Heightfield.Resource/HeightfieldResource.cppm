/// Foundation::Heightfield.Resource - the `foundation.heightfield.resource` module.
///
/// The heightfield as a referenceable, cooked resource: HeightfieldSource (the serialized cooked
/// form - size + world footprint + Y range + the raw u16 blob) is built by HeightfieldFactory into
/// the runtime Heightfield (foundation.heightfield). Terrain, the physics collider, and the nav bake
/// all resolve Ref<Heightfield> to this product; no one embeds the raw grid. The blob is inline in
/// the (binary) cooked product; the ASSET side (Heightfield.Pipeline) keeps the source heightmap as
/// a sidecar per the bulk-data rule.

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
    /// Cooked heightfield: the grid parameters + the raw u16 sample bytes (row-major, size*size*2).
    class HeightfieldSource final : public ISerializable
    {
        RTTI_OBJECT(HeightfieldSource, ISerializable)
    public:
        i32 size = 0;
        Float2 worldSize{0.0f, 0.0f};
        f32 minY = 0.0f;
        f32 maxY = 0.0f;
        Array<u8> heightBlob; // raw Height (u16) bytes, size*size*2

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "size", size);
            foundation::core::Serialize(ar, "worldSize", worldSize);
            foundation::core::Serialize(ar, "minY", minY);
            foundation::core::Serialize(ar, "maxY", maxY);
            foundation::core::Serialize(ar, "heightBlob", heightBlob);
        }

        /// Capture a runtime Heightfield into this source (for cooking).
        static void FromHeightfield(const Heightfield& hf, HeightfieldSource& out)
        {
            out.size = hf.Size();
            out.worldSize = hf.WorldSize();
            out.minY = hf.MinY();
            out.maxY = hf.MaxY();
            const Span<const Height> samples = hf.Samples();
            out.heightBlob.Clear();
            out.heightBlob.Resize(samples.Size() * sizeof(Height));
            if (!samples.IsEmpty())
            {
                MemCopy(out.heightBlob.Data(), samples.Data(), samples.Size() * sizeof(Height));
            }
        }

        /// Build the runtime product. Returns an empty grid if the cooked data is inconsistent
        /// (invalid size or a blob that does not match size*size*2) rather than a malformed grid.
        [[nodiscard]] RefPtr<Heightfield> Build() const
        {
            if (!IsValidSize(size))
            {
                return MakeRef<Heightfield>(DefaultAllocator());
            }
            const usize expected = static_cast<usize>(size) * static_cast<usize>(size) * sizeof(Height);
            if (heightBlob.Size() != expected)
            {
                return MakeRef<Heightfield>(DefaultAllocator());
            }
            RefPtr<Heightfield> hf =
                MakeRef<Heightfield>(DefaultAllocator(), size, worldSize, minY, maxY);
            MemCopy(hf->Samples().Data(), heightBlob.Data(), expected);
            return hf;
        }
    };

    /// Builds a HeightfieldSource into a runtime Heightfield. Pure-CPU (no GPU state - the terrain
    /// renderer owns the height texture, cached per resource), so the whole build runs on a worker.
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
            return src->Build();
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
