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
    // Cooked record: the serialized navmesh blob (from NavigationMeshBuilder::Build). Produced by
    // the NavigationZoneAssetBuilder, bound at runtime through NavigationZoneResource.
    class NavigationZoneSource : public ISerializable
    {
        RTTI_OBJECT(NavigationZoneSource, ISerializable)
    public:
        Array<u8> navMeshBlob; // header + Detour tile

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "navMeshBlob", navMeshBlob);
        }
    };

    // Runtime product: the loaded navmesh a zone component binds. Invalid (IsValid()==false) when
    // the blob was empty or malformed - the subsystem skips such a zone.
    class NavigationZoneResource : public Object
    {
        RTTI_OBJECT(NavigationZoneResource, Object)
    public:
        NavigationMesh mesh;

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

    RTTI_DEFINE_OBJECT(NavigationZoneSource, "rtti::navigation")
    RTTI_DEFINE_OBJECT(NavigationZoneResource, "rtti::navigation")
}
