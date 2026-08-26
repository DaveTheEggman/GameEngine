/// Foundation::Terrain.Resource - the `foundation.terrain.resource` module.
///
/// The cooked terrain resource: a bundle of REFERENCES (a heightfield, the top-K splat weights,
/// an explicit BASE layer + an unbounded paint PALETTE of albedo layers) + a cast-shadows flag -
/// NOT embedded bulk. TerrainSource is the serialized form (guids + params, the Material.Resource
/// pattern); TerrainFactory resolves each guid through the manager into the runtime
/// TerrainResource. TerrainComponent (engine.terrain) references Ref<TerrainResource>. The
/// heightfield is the shared source of truth - physics/nav resolve the SAME Ref<Heightfield>.
/// The BASE layer is what shows wherever painted weights do not sum to 1 (terrain-splat-topk.md);
/// it is not part of the palette and is never painted directly.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.terrain.resource;

import foundation.core;
import foundation.resource;
import foundation.content;
import foundation.heightfield;
import foundation.texture.resource;

export import :splatmap; // SplatWeights (top-K rasters) + brush cores + cooked source/factory

using namespace foundation::core;
using namespace foundation::resource;

export namespace foundation::terrain
{
    /// The cooked terrain (serialized): the referenced resources by guid + per-layer tiling +
    /// cast-shadows. The factory resolves the guids; nothing here is bulk data.
    /// DataVersion 2 = the top-K model (base + palette + weightsId). Version 1 (the fixed-4-layer
    /// model: splatmapId + layerAlbedoIds, where layer 0 was the de-facto base) upgrades on read:
    /// base = old layer 0, palette = old layers 1.., weightsId = old splatmapId (the weights
    /// product itself migrates in SplatWeightsFactory).
    class TerrainSource final : public ISerializable
    {
        RTTI_OBJECT(TerrainSource, ISerializable)
    public:
        Guid heightfieldId;           // the shared Ref<Heightfield> (also used by physics/nav)
        Guid weightsId;               // the SplatWeights product (nil = none: pure base)
        Guid baseAlbedoId;            // the BASE layer albedo (nil = white dummy)
        f32 baseTileScale = 1.0f;
        Array<Guid> paletteAlbedoIds; // paint layers, unbounded (parallel to paletteTileScales)
        Array<f32> paletteTileScales;
        bool castShadows = true;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "heightfieldId", heightfieldId);
            if (ar.Version() >= 2)
            {
                foundation::core::Serialize(ar, "weightsId", weightsId);
                foundation::core::Serialize(ar, "baseAlbedoId", baseAlbedoId);
                foundation::core::Serialize(ar, "baseTileScale", baseTileScale);
                foundation::core::Serialize(ar, "paletteAlbedoIds", paletteAlbedoIds);
                foundation::core::Serialize(ar, "paletteTileScales", paletteTileScales);
            }
            else
            {
                // v1 (fixed-4-layer): splatmapId + layer arrays, layer 0 = the de-facto base.
                // Map on read; writes always run at the CURRENT version (>= 2 above).
                Array<Guid> layerAlbedoIds;
                Array<f32> layerTileScales;
                foundation::core::Serialize(ar, "splatmapId", weightsId);
                foundation::core::Serialize(ar, "layerAlbedoIds", layerAlbedoIds);
                foundation::core::Serialize(ar, "layerTileScales", layerTileScales);
                baseAlbedoId = layerAlbedoIds.Size() > 0 ? layerAlbedoIds[0] : Guid{};
                baseTileScale = layerTileScales.Size() > 0 ? layerTileScales[0] : 1.0f;
                paletteAlbedoIds.Clear();
                paletteTileScales.Clear();
                for (usize i = 1; i < layerAlbedoIds.Size(); ++i)
                {
                    paletteAlbedoIds.PushBack(layerAlbedoIds[i]);
                    paletteTileScales.PushBack(i < layerTileScales.Size() ? layerTileScales[i]
                                                                          : 1.0f);
                }
            }
            foundation::core::Serialize(ar, "castShadows", castShadows);
        }
    };

    /// The cook-built paint-palette texel data: every palette albedo resized to ONE common slice
    /// size and mip-chained, concatenated slice-major (slice 0 mips 0..M-1, slice 1 mips ...).
    /// CPU-side (the heightfield/weights model): engine.terrain derives + caches the GPU
    /// Texture2DArray keyed by `uid`. Rides the terrain's own cooked instance as the
    /// `kPaletteStream` sidecar - no extra product guid.
    class TerrainPaletteData final : public Object
    {
        RTTI_OBJECT(TerrainPaletteData, Object)
    public:
        const u64 uid = SplatWeights::NextUid(); // shared uid domain; cache key (never a pointer)

        u32 sliceSize = 0;  // square slice side (power of two)
        u32 mipCount = 0;   // mips per slice (down to 1x1)
        u32 sliceCount = 0; // == the palette layer count at cook time
        Array<u8> texels;   // RGBA8, slice-major, each slice = its full mip chain

        /// Total bytes of one slice's full mip chain for `sliceSize`/`mipCount`.
        [[nodiscard]] static usize SliceBytes(u32 sliceSize, u32 mipCount) noexcept
        {
            usize total = 0;
            u32 dim = sliceSize;
            for (u32 m = 0; m < mipCount; ++m)
            {
                total += static_cast<usize>(dim) * dim * 4u;
                dim = dim > 1 ? dim / 2 : 1;
            }
            return total;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return sliceSize > 0 && mipCount > 0 && sliceCount > 0 &&
                   texels.Size() == SliceBytes(sliceSize, mipCount) * sliceCount;
        }
    };

    /// The terrain cooked instance's palette sidecar stream (see TerrainPaletteData).
    inline constexpr StringView kPaletteStream = u8"palette";

    /// The runtime terrain product: the resolved resource handles the renderer draws with. The
    /// heightfield drives geometry (via foundation.terrain's chunk model) + the shared collision.
    class TerrainResource final : public Object
    {
        RTTI_OBJECT(TerrainResource, Object)
    public:
        struct Layer
        {
            // Ref (not Proxy): the cooked factory binds it as a proxy, but in-memory/procedural
            // terrain (playgrounds, tests, runtime generation) can assign a product directly.
            Ref<texture::Texture> albedo;
            f32 tileScale = 1.0f;
        };

        Ref<heightfield::Heightfield> heightfield;
        Ref<SplatWeights> weights; // the top-K paint rasters (nil = pure base)
        Layer base;                // shows wherever paint doesn't sum to 1; never painted
        Array<Layer> palette;      // the paint layers, unbounded (8-bit index -> <= 256 used)
        // The cook-built palette texel array (null = no palette or an in-memory terrain that
        // assigns it directly); engine.terrain uploads it as the Texture2DArray.
        RefPtr<TerrainPaletteData> paletteData;
        bool castShadows = true;

        [[nodiscard]] u32 PaletteCount() const noexcept
        {
            return static_cast<u32>(palette.Size());
        }
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
            // Stamp each ref's serialized identity (its source guid) as well as binding the proxy:
            // Ref<T>::id exists precisely to carry this, and factory-built products must round-trip
            // their sub-ref ids (serializing one otherwise writes nil) - and the editor resolves the
            // heightfield asset from TerrainResource.heightfield.id (product guid == source guid).
            if (!src->heightfieldId.IsNil())
            {
                terrain->heightfield.SetProxy(
                    manager.Bind<heightfield::Heightfield>(src->heightfieldId));
                terrain->heightfield.SetId(src->heightfieldId);
            }
            if (!src->weightsId.IsNil())
            {
                terrain->weights.SetProxy(manager.Bind<SplatWeights>(src->weightsId));
                terrain->weights.SetId(src->weightsId);
            }
            terrain->base.tileScale = src->baseTileScale;
            if (!src->baseAlbedoId.IsNil())
            {
                terrain->base.albedo.SetProxy(manager.Bind<texture::Texture>(src->baseAlbedoId));
                terrain->base.albedo.SetId(src->baseAlbedoId);
            }
            for (usize i = 0; i < src->paletteAlbedoIds.Size(); ++i)
            {
                TerrainResource::Layer layer;
                layer.tileScale =
                    (i < src->paletteTileScales.Size()) ? src->paletteTileScales[i] : 1.0f;
                if (!src->paletteAlbedoIds[i].IsNil())
                {
                    layer.albedo.SetProxy(
                        manager.Bind<texture::Texture>(src->paletteAlbedoIds[i]));
                    layer.albedo.SetId(src->paletteAlbedoIds[i]);
                }
                terrain->palette.PushBack(Move(layer));
            }
            // The cook-built palette texel array (kPaletteStream on this same cooked instance).
            if (UniquePtr<IStream> stream = instance.ReadData(kPaletteStream))
            {
                const i64 size = stream->Size();
                if (size > static_cast<i64>(3 * sizeof(u32)))
                {
                    u32 header[3] = {0, 0, 0}; // sliceSize, mipCount, sliceCount
                    if (stream->Read(header, sizeof(header)) == sizeof(header))
                    {
                        RefPtr<TerrainPaletteData> palette =
                            MakeRef<TerrainPaletteData>(DefaultAllocator());
                        palette->sliceSize = header[0];
                        palette->mipCount = header[1];
                        palette->sliceCount = header[2];
                        const usize texelBytes = static_cast<usize>(size) - sizeof(header);
                        palette->texels.Resize(texelBytes);
                        if (stream->Read(palette->texels.Data(),
                                         static_cast<u64>(texelBytes)) == texelBytes &&
                            palette->IsValid())
                        {
                            terrain->paletteData = Move(palette);
                        }
                    }
                }
            }
            return terrain;
        }
    };

    /// Register the terrain resource types (product + cooked source) for load.
    inline void RegisterTerrainResourceTypes()
    {
        GlobalTypeRegistry().Register(TerrainResource::StaticType());
        GlobalTypeRegistry().Register(TerrainPaletteData::StaticType());
        GlobalTypeRegistry().Register(TerrainSource::StaticType());
        RegisterSerializable<TerrainSource>();
    }

    RTTI_DEFINE_OBJECT(TerrainResource, "rtti::terrain")
    RTTI_DEFINE_OBJECT(TerrainPaletteData, "rtti::terrain")
    RTTI_DEFINE_OBJECT_VERSIONED(TerrainSource, "rtti::terrain", 2)
}
