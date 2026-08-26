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
    /// DataVersion 4 = per-layer HEIGHT (displacement) maps + heightBlendContrast (terrain-height-blend.md;
    /// all new ids default nil + contrast 0.25 -> the OFF path, so v3 payloads are visually unchanged).
    /// DataVersion 3 = per-layer normal + ORM maps (terrain-layer-pbr.md; all new ids default nil ->
    /// flat normal / default ORM, so v2 payloads are visually unchanged). DataVersion 2 = the top-K
    /// model (base + palette + weightsId). Version 1 (the fixed-4-layer model: splatmapId +
    /// layerAlbedoIds, where layer 0 was the de-facto base) upgrades on read: base = old layer 0,
    /// palette = old layers 1.., weightsId = old splatmapId (the weights product itself migrates in
    /// SplatWeightsFactory).
    class TerrainSource final : public ISerializable
    {
        RTTI_OBJECT(TerrainSource, ISerializable)
    public:
        Guid heightfieldId;           // the shared Ref<Heightfield> (also used by physics/nav)
        Guid weightsId;               // the SplatWeights product (nil = none: pure base)
        Guid baseAlbedoId;            // the BASE layer albedo (nil = white dummy)
        Guid baseNormalId;            // the BASE normal map (nil = flat); DataVersion 3
        Guid baseOrmId;               // the BASE ORM (nil = 1,1,0 default); DataVersion 3
        Guid baseHeightId;            // the BASE height/displacement map (nil = dummy); DataVersion 4
        f32 baseTileScale = 1.0f;
        Array<Guid> paletteAlbedoIds; // paint layers, unbounded (parallel to paletteTileScales)
        Array<Guid> paletteNormalIds; // per-palette-layer normal map (nil allowed); DataVersion 3
        Array<Guid> paletteOrmIds;    // per-palette-layer ORM (nil allowed); DataVersion 3
        Array<Guid> paletteHeightIds; // per-palette-layer height map (nil allowed); DataVersion 4
        Array<f32> paletteTileScales;
        // Height-blend soft-skirt width (terrain-height-blend.md; consulted only when height maps are
        // present). 0..1; small = sharp interlock, large = washes toward an equal mix. DataVersion 4.
        f32 heightBlendContrast = 0.25f;
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
                if (ar.Version() >= 3) // per-layer normal + ORM maps (terrain-layer-pbr.md)
                {
                    foundation::core::Serialize(ar, "baseNormalId", baseNormalId);
                    foundation::core::Serialize(ar, "baseOrmId", baseOrmId);
                    foundation::core::Serialize(ar, "paletteNormalIds", paletteNormalIds);
                    foundation::core::Serialize(ar, "paletteOrmIds", paletteOrmIds);
                }
                if (ar.Version() >= 4) // per-layer height maps + contrast (terrain-height-blend.md)
                {
                    foundation::core::Serialize(ar, "baseHeightId", baseHeightId);
                    foundation::core::Serialize(ar, "paletteHeightIds", paletteHeightIds);
                    foundation::core::Serialize(ar, "heightBlendContrast", heightBlendContrast);
                }
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
        Array<u8> texels;   // ALBEDO array: RGBA8, slice-major, each slice = its full mip chain
        // The NORMAL / ORM arrays (terrain-layer-pbr.md), built ON DEMAND: empty = absent (no palette
        // layer used that map -> the renderer binds a 1x1 dummy). Present arrays share the albedo's
        // sliceSize/mipCount/sliceCount geometry. The renderer ignores these until the P1 shader lands.
        Array<u8> normalTexels;
        Array<u8> ormTexels;
        // The HEIGHT/displacement array (terrain-height-blend.md), built ON DEMAND: empty = absent (no
        // palette layer used a height map -> the renderer binds a 1x1 mid-height dummy). Same geometry
        // as albedo; read via the red channel.
        Array<u8> heightTexels;

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

        /// One array's expected byte size at this geometry.
        [[nodiscard]] usize ArrayBytes() const noexcept
        {
            return SliceBytes(sliceSize, mipCount) * sliceCount;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return sliceSize > 0 && mipCount > 0 && sliceCount > 0 && texels.Size() == ArrayBytes();
        }
        [[nodiscard]] bool HasNormal() const noexcept
        {
            return IsValid() && normalTexels.Size() == ArrayBytes();
        }
        [[nodiscard]] bool HasOrm() const noexcept
        {
            return IsValid() && ormTexels.Size() == ArrayBytes();
        }
        [[nodiscard]] bool HasHeight() const noexcept
        {
            return IsValid() && heightTexels.Size() == ArrayBytes();
        }
    };

    /// The terrain cooked instance's palette sidecar streams (see TerrainPaletteData). The normal /
    /// ORM / height streams are ABSENT when no palette layer uses that map (terrain-layer-pbr.md R4).
    inline constexpr StringView kPaletteStream = u8"palette";
    inline constexpr StringView kPaletteNormalStream = u8"palette.normal";
    inline constexpr StringView kPaletteOrmStream = u8"palette.orm";
    inline constexpr StringView kPaletteHeightStream = u8"palette.height";

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
            Ref<texture::Texture> normal; // tangent-space normal map (nil = flat); terrain-layer-pbr.md
            Ref<texture::Texture> orm;    // R=AO G=roughness B=metallic (nil = 1,1,0 default)
            Ref<texture::Texture> height; // displacement map for height-blend (nil = dummy); .r used
            f32 tileScale = 1.0f;         // shared by all maps of this layer
        };

        Ref<heightfield::Heightfield> heightfield;
        Ref<SplatWeights> weights; // the top-K paint rasters (nil = pure base)
        Layer base;                // shows wherever paint doesn't sum to 1; never painted
        Array<Layer> palette;      // the paint layers, unbounded (8-bit index -> <= 256 used)
        // The cook-built palette texel array (null = no palette or an in-memory terrain that
        // assigns it directly); engine.terrain uploads it as the Texture2DArray.
        RefPtr<TerrainPaletteData> paletteData;
        // Height-blend soft-skirt width (terrain-height-blend.md), copied from the source; the renderer
        // feeds it to the shader only when height maps are present.
        f32 heightBlendContrast = 0.25f;
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
            terrain->heightBlendContrast = src->heightBlendContrast;
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
            // Bind + stamp a texture ref from its source guid (nil = leave unbound -> renderer dummy).
            auto bind = [&manager](Ref<texture::Texture>& ref, const Guid& id)
            {
                if (!id.IsNil())
                {
                    ref.SetProxy(manager.Bind<texture::Texture>(id));
                    ref.SetId(id);
                }
            };
            terrain->base.tileScale = src->baseTileScale;
            bind(terrain->base.albedo, src->baseAlbedoId);
            bind(terrain->base.normal, src->baseNormalId);
            bind(terrain->base.orm, src->baseOrmId);
            bind(terrain->base.height, src->baseHeightId);
            for (usize i = 0; i < src->paletteAlbedoIds.Size(); ++i)
            {
                TerrainResource::Layer layer;
                layer.tileScale =
                    (i < src->paletteTileScales.Size()) ? src->paletteTileScales[i] : 1.0f;
                bind(layer.albedo, src->paletteAlbedoIds[i]);
                if (i < src->paletteNormalIds.Size())
                {
                    bind(layer.normal, src->paletteNormalIds[i]);
                }
                if (i < src->paletteOrmIds.Size())
                {
                    bind(layer.orm, src->paletteOrmIds[i]);
                }
                if (i < src->paletteHeightIds.Size())
                {
                    bind(layer.height, src->paletteHeightIds[i]);
                }
                terrain->palette.PushBack(Move(layer));
            }
            // The cook-built palette texel arrays: albedo (kPaletteStream) + optional normal / ORM
            // sidecars on this same cooked instance (each a {sliceSize, mipCount, sliceCount} header +
            // texels; absent = no array, terrain-layer-pbr.md R4/R5). The renderer ignores normal/ORM
            // until the P1 shader lands.
            u32 hdr[3] = {0, 0, 0};
            Array<u8> texels;
            if (ReadArrayStream(instance, kPaletteStream, hdr, texels))
            {
                RefPtr<TerrainPaletteData> palette = MakeRef<TerrainPaletteData>(DefaultAllocator());
                palette->sliceSize = hdr[0];
                palette->mipCount = hdr[1];
                palette->sliceCount = hdr[2];
                palette->texels = Move(texels);
                if (palette->IsValid())
                {
                    const usize expect = palette->ArrayBytes();
                    u32 nhdr[3] = {0, 0, 0};
                    Array<u8> ntex;
                    if (ReadArrayStream(instance, kPaletteNormalStream, nhdr, ntex) &&
                        nhdr[0] == hdr[0] && nhdr[1] == hdr[1] && nhdr[2] == hdr[2] &&
                        ntex.Size() == expect)
                    {
                        palette->normalTexels = Move(ntex);
                    }
                    u32 ohdr[3] = {0, 0, 0};
                    Array<u8> otex;
                    if (ReadArrayStream(instance, kPaletteOrmStream, ohdr, otex) &&
                        ohdr[0] == hdr[0] && ohdr[1] == hdr[1] && ohdr[2] == hdr[2] &&
                        otex.Size() == expect)
                    {
                        palette->ormTexels = Move(otex);
                    }
                    u32 hhdr[3] = {0, 0, 0};
                    Array<u8> htex;
                    if (ReadArrayStream(instance, kPaletteHeightStream, hhdr, htex) &&
                        hhdr[0] == hdr[0] && hhdr[1] == hdr[1] && hhdr[2] == hdr[2] &&
                        htex.Size() == expect)
                    {
                        palette->heightTexels = Move(htex);
                    }
                    terrain->paletteData = Move(palette);
                }
            }
            return terrain;
        }

    private:
        // Read a "{sliceSize, mipCount, sliceCount} u32 header + texels" palette stream. Returns false
        // if the stream is absent or truncated; on success fills `outHeader` + `outTexels`.
        [[nodiscard]] static bool ReadArrayStream(foundation::content::Instance& instance,
                                                  StringView name, u32 outHeader[3],
                                                  Array<u8>& outTexels)
        {
            UniquePtr<IStream> stream = instance.ReadData(name);
            if (!stream)
            {
                return false;
            }
            const i64 size = stream->Size();
            if (size <= static_cast<i64>(sizeof(u32) * 3))
            {
                return false;
            }
            if (stream->Read(outHeader, sizeof(u32) * 3) != sizeof(u32) * 3)
            {
                return false;
            }
            const usize texelBytes = static_cast<usize>(size) - sizeof(u32) * 3;
            outTexels.Resize(texelBytes);
            return stream->Read(outTexels.Data(), static_cast<u64>(texelBytes)) == texelBytes;
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
    RTTI_DEFINE_OBJECT_VERSIONED(TerrainSource, "rtti::terrain", 4)
}
