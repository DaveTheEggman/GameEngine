// Foundation::Navigation.Resource - the `foundation.navigation.resource` module.
//
// Cooked navigation content (Documentation/Plans/navigation.md), mirroring the collision-shape
// split:
//   * NavigationZoneSource - the cooked record: the serialized single-tile navmesh blob a bake
//     action produced (NavigationMeshBuilder::Build output).
//   * NavigationZoneResource       - the runtime product a NavMeshZoneComponent's Ref<> binds: the blob
//     loaded into a live NavigationMesh, ready for query/crowd construction.
//
// A zone's navmesh is baked in ZONE-LOCAL space (relative to the zone entity's transform at bake
// time); the engine transforms queries through the current transform, so the cooked data is
// placement-independent and rides a prefab to any location without a rebake.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.navigation.resource;

import foundation.core;
import foundation.resource;
import foundation.content;
import foundation.navigation;

using namespace foundation::core;
namespace resource = foundation::resource;

export namespace foundation::navigation
{
    /// The bake-frame convention stamp (see bakedFrame below). 0 = legacy (pre-2026-08-29: the
    /// bake divided geometry by the zone's FULL world matrix, scale included); 1 = rigid (the bake
    /// and the runtime both use the scale-free RigidPart frame). The two agree exactly on
    /// unit-scale zone entities and desync on scaled ones.
    inline constexpr u32 kNavigationZoneFrameRigid = 1;

    // Cooked record: the serialized navmesh blob (from NavigationMeshBuilder::Build). Produced by
    // the NavigationZoneAssetBuilder, bound at runtime through NavigationZoneResource.
    class NavigationZoneSource : public ISerializable
    {
        RTTI_OBJECT(NavigationZoneSource, ISerializable)
    public:
        Array<u8> navMeshBlob; // header + Detour tile
        u32 bakedFrame = 0;    // frame convention the bake used; 0 = legacy (see above)

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "navMeshBlob", navMeshBlob);
            // v1: the frame-convention stamp. Legacy payloads (version 0) read as bakedFrame 0,
            // which the subsystem flags on scaled zone entities (rigid-frame desync -> re-bake).
            if (ar.Version() >= 1)
            {
                foundation::core::Serialize(ar, "bakedFrame", bakedFrame);
            }
        }
    };

    // Runtime product: the loaded navmesh a zone component binds. Invalid (IsValid()==false) when
    // the blob was empty or malformed - the subsystem skips such a zone.
    class NavigationZoneResource : public Object
    {
        RTTI_OBJECT(NavigationZoneResource, Object)
    public:
        NavigationMesh mesh;
        u32 bakedFrame = 0; // frame convention of the bake (kNavigationZoneFrameRigid = current)

        [[nodiscard]] bool IsValid() const noexcept { return mesh.IsValid(); }
    };

    class NavigationZoneFactory final : public resource::IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &NavigationZoneResource::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(resource::ResourceManager&,
                                            foundation::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            NavigationZoneSource* source = Cast<NavigationZoneSource>(object.Get());
            if (source == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<NavigationZoneResource> zone = MakeRef<NavigationZoneResource>(DefaultAllocator());
            zone->bakedFrame = source->bakedFrame;
            if (!source->navMeshBlob.IsEmpty())
            {
                // A malformed blob leaves the mesh invalid; the product still constructs so the
                // bind resolves (the subsystem simply gets no navmesh for that zone).
                (void)zone->mesh.Load(Span<const byte>{
                    reinterpret_cast<const byte*>(source->navMeshBlob.Data()),
                    source->navMeshBlob.Size()});
            }
            return zone;
        }
    };

    // Registers the cooked record + product types (content-DB construction by type name).
    inline void RegisterNavigationResource()
    {
        GlobalTypeRegistry().Register(NavigationZoneSource::StaticType());
        RegisterSerializable<NavigationZoneSource>();
        GlobalTypeRegistry().Register(NavigationZoneResource::StaticType());
    }

    RTTI_DEFINE_OBJECT_VERSIONED(NavigationZoneSource, "rtti::navigation", 1) // v1: bakedFrame stamp
    RTTI_DEFINE_OBJECT(NavigationZoneResource, "rtti::navigation")
}
