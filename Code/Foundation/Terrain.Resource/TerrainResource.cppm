/// Foundation::Terrain.Resource - the `foundation.terrain.resource` module.
///
/// The cooked terrain resource: a bundle of REFERENCES (a heightfield, a splatmap, per-layer albedo
/// textures) + a cast-shadows flag - NOT embedded bulk. TerrainSource is the serialized form (guids +
/// params, the Material.Resource pattern); TerrainFactory resolves each guid through the manager into
/// the runtime TerrainResource. TerrainComponent (engine.terrain) references Ref<TerrainResource>. The
/// heightfield is the shared source of truth - physics/nav resolve the SAME Ref<Heightfield>.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.terrain.resource;

import foundation.core;
import foundation.resource;
import foundation.content;
import foundation.heightfield;
import foundation.texture.resource;

using namespace foundation::core;
using namespace foundation::resource;

export namespace foundation::terrain
{
    /// The cooked terrain (serialized): the referenced resources by guid + per-layer tiling +
    /// cast-shadows. The factory resolves the guids; nothing here is bulk data.
    class TerrainSource final : public ISerializable
    {
        RTTI_OBJECT(TerrainSource, ISerializable)
    public:
        Guid heightfieldId;               // the shared Ref<Heightfield> (also used by physics/nav)
        Guid splatmapId;                  // one RGBA splatmap (up to 4 layers in P1); nil = none
        Array<Guid> layerAlbedoIds;       // per-layer albedo texture (parallel to layerTileScales)
        Array<f32> layerTileScales;       // per-layer UV tiling
        bool castShadows = true;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "heightfieldId", heightfieldId);
            foundation::core::Serialize(ar, "splatmapId", splatmapId);
            foundation::core::Serialize(ar, "layerAlbedoIds", layerAlbedoIds);
            foundation::core::Serialize(ar, "layerTileScales", layerTileScales);
            foundation::core::Serialize(ar, "castShadows", castShadows);
        }
    };

    /// The runtime terrain product: the resolved resource handles the renderer draws with. The
    /// heightfield drives geometry (via foundation.terrain's chunk model) + the shared collision.
    class TerrainResource final : public Object
    {
        RTTI_OBJECT(TerrainResource, Object)
    public:
        struct Layer
        {
            Proxy<texture::Texture> albedo;
            f32 tileScale = 1.0f;
        };

        Proxy<heightfield::Heightfield> heightfield;
        Proxy<texture::Texture> splatmap;
        Array<Layer> layers;
        bool castShadows = true;

        [[nodiscard]] u32 LayerCount() const noexcept { return static_cast<u32>(layers.Size()); }
    };

    /// Resolves a TerrainSource into a runtime TerrainResource, binding each referenced resource through the
    /// manager (recording the dependency edges, so a heightfield/texture reload cascades). A missing
    /// sub-resource (not cooked, or no GPU factory in headless tools) leaves that handle unbound.
    class TerrainFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &TerrainResource::StaticType(); }

        [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager,
                                            foundation::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            TerrainSource* src = Cast<TerrainSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<TerrainResource> terrain = MakeRef<TerrainResource>(DefaultAllocator());
            terrain->castShadows = src->castShadows;
            if (!src->heightfieldId.IsNil())
            {
                terrain->heightfield = manager.Bind<heightfield::Heightfield>(src->heightfieldId);
            }
            if (!src->splatmapId.IsNil())
            {
                terrain->splatmap = manager.Bind<texture::Texture>(src->splatmapId);
            }
            for (usize i = 0; i < src->layerAlbedoIds.Size(); ++i)
            {
                TerrainResource::Layer layer;
                layer.tileScale =
                    (i < src->layerTileScales.Size()) ? src->layerTileScales[i] : 1.0f;
                if (!src->layerAlbedoIds[i].IsNil())
                {
                    layer.albedo = manager.Bind<texture::Texture>(src->layerAlbedoIds[i]);
                }
                terrain->layers.PushBack(Move(layer));
            }
            return terrain;
        }
    };

    /// Register the terrain resource types (product + cooked source) for load.
    inline void RegisterTerrainResourceTypes()
    {
        GlobalTypeRegistry().Register(TerrainResource::StaticType());
        GlobalTypeRegistry().Register(TerrainSource::StaticType());
        RegisterSerializable<TerrainSource>();
    }

    RTTI_DEFINE_OBJECT(TerrainResource, "rtti::terrain")
    RTTI_DEFINE_OBJECT_VERSIONED(TerrainSource, "rtti::terrain", 1)
}
