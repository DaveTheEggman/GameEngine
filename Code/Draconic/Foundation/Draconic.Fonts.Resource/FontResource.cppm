// Draconic::FontsResource - the `draconic.fonts.resource` module (runtime).
//
// The FONT as a runtime resource - the triad tier the original port skipped
// ("Ported from Sedulous.Fonts (excluding Fonts.Resources)"; roadmap: "Fonts -> proper
// triad"). Same model-A shape as draconic.texture.resource:
//
//   * FontResource (ISerializable): the cooked *record* loaded from the output DB - the
//     family, the atlas pixel mode (Alpha8 coverage or RGBA MSDF), and one ENTRY per baked
//     pixel size (metrics + glyph/kerning/region tables + atlas metadata). The atlas pixel
//     payloads live concatenated in the "data" stream, per-entry offset+size recorded.
//   * Font (Object): the runtime product - owns, per entry, a rasterizer-free BakedFont,
//     its IFontAtlas (BakedFontAtlas for coverage, DFFontAtlas for MSDF), and the atlas as
//     an image::OwnedImageData ready for a renderer to upload (the same RGBA expansion the
//     TTF service performs). A shipped game loads THIS and never touches stb_truetype.
//   * FontFactory (IResourceFactory): cooked record + stream -> Font. DEVICE-FREE - the VG
//     layer uploads atlas images itself, so fonts bind headlessly.
//
// The runtime never links the editor/source side; it loads only cooked resources.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

export module draconic.fonts.resource;

import draconic.core;
import draconic.fonts;
import draconic.fonts.baked;
import draconic.fonts.distancefield;
import draconic.image;
import draconic.content;
import draconic.resource;

using namespace draconic::core;
using namespace draconic::resource;

export namespace draconic::fonts
{
    namespace image = draconic::image;

    // How the cooked atlas payload is encoded (uniform across a resource's entries).
    enum class FontResourcePixels : u32
    {
        Alpha8,        // single-channel coverage (expanded to RGBA8 at load, like the TTF path)
        DistanceField, // RGBA8 MSDF channels, linear (no sRGB decode - geometric data)
    };

    // -- serialized table rows ----------------------------------------------------------

    struct FontResourceGlyph
    {
        i32 codepoint = 0;
        GlyphInfo info;

        void Serialize(ISerializer& ar)
        {
            draconic::core::Serialize(ar, "codepoint", codepoint);
            draconic::core::Serialize(ar, "glyphIndex", info.glyphIndex);
            draconic::core::Serialize(ar, "advanceWidth", info.advanceWidth);
            draconic::core::Serialize(ar, "leftSideBearing", info.leftSideBearing);
            draconic::core::Serialize(ar, "bbX", info.boundingBox.x);
            draconic::core::Serialize(ar, "bbY", info.boundingBox.y);
            draconic::core::Serialize(ar, "bbW", info.boundingBox.width);
            draconic::core::Serialize(ar, "bbH", info.boundingBox.height);
            draconic::core::Serialize(ar, "hasBitmap", info.hasBitmap);
            if (ar.Mode() == SerializeMode::Read)
            {
                info.codepoint = codepoint;
            }
        }
    };
    inline void Serialize(ISerializer& ar, FontResourceGlyph& g)
    {
        ar.BeginObject();
        g.Serialize(ar);
        ar.EndObject();
    }

    struct FontResourceKerning
    {
        i32 first = 0;
        i32 second = 0;
        f32 amount = 0.0f;

        void Serialize(ISerializer& ar)
        {
            draconic::core::Serialize(ar, "first", first);
            draconic::core::Serialize(ar, "second", second);
            draconic::core::Serialize(ar, "amount", amount);
        }
    };
    inline void Serialize(ISerializer& ar, FontResourceKerning& k)
    {
        ar.BeginObject();
        k.Serialize(ar);
        ar.EndObject();
    }

    struct FontResourceRegion
    {
        i32 codepoint = 0;
        AtlasRegion region;

        void Serialize(ISerializer& ar)
        {
            draconic::core::Serialize(ar, "codepoint", codepoint);
            draconic::core::Serialize(ar, "x", region.x);
            draconic::core::Serialize(ar, "y", region.y);
            draconic::core::Serialize(ar, "width", region.width);
            draconic::core::Serialize(ar, "height", region.height);
            draconic::core::Serialize(ar, "offsetX", region.offsetX);
            draconic::core::Serialize(ar, "offsetY", region.offsetY);
            draconic::core::Serialize(ar, "advanceX", region.advanceX);
        }
    };
    inline void Serialize(ISerializer& ar, FontResourceRegion& r)
    {
        ar.BeginObject();
        r.Serialize(ar);
        ar.EndObject();
    }

    // One baked pixel size: metrics + tables + atlas metadata; pixels are the
    // [pixelOffset, pixelOffset + pixelBytes) slice of the resource's "data" stream.
    struct FontResourceEntry
    {
        f32 pixelHeight = 0.0f;

        // FontMetrics, flattened (lineHeight/decorations recompute from these on load).
        f32 ascent = 0.0f;
        f32 descent = 0.0f;
        f32 lineGap = 0.0f;
        f32 scale = 1.0f;

        Array<FontResourceGlyph> glyphs;
        Array<FontResourceKerning> kerning;
        Array<FontResourceRegion> regions;

        u32 atlasWidth = 0;
        u32 atlasHeight = 0;
        f32 whitePixelU = 0.0f;
        f32 whitePixelV = 0.0f;
        f32 dfPixelRange = 4.0f; // DistanceField mode only

        u64 pixelOffset = 0;
        u64 pixelBytes = 0;

        void Serialize(ISerializer& ar)
        {
            draconic::core::Serialize(ar, "pixelHeight", pixelHeight);
            draconic::core::Serialize(ar, "ascent", ascent);
            draconic::core::Serialize(ar, "descent", descent);
            draconic::core::Serialize(ar, "lineGap", lineGap);
            draconic::core::Serialize(ar, "scale", scale);
            draconic::core::Serialize(ar, "glyphs", glyphs);
            draconic::core::Serialize(ar, "kerning", kerning);
            draconic::core::Serialize(ar, "regions", regions);
            draconic::core::Serialize(ar, "atlasWidth", atlasWidth);
            draconic::core::Serialize(ar, "atlasHeight", atlasHeight);
            draconic::core::Serialize(ar, "whitePixelU", whitePixelU);
            draconic::core::Serialize(ar, "whitePixelV", whitePixelV);
            draconic::core::Serialize(ar, "dfPixelRange", dfPixelRange);
            draconic::core::Serialize(ar, "pixelOffset", pixelOffset);
            draconic::core::Serialize(ar, "pixelBytes", pixelBytes);
        }
    };
    inline void Serialize(ISerializer& ar, FontResourceEntry& e)
    {
        ar.BeginObject();
        e.Serialize(ar);
        ar.EndObject();
    }

    // Cooked font record (output DB). Atlas pixels are the "data" stream.
    class FontResource final : public ISerializable
    {
        DRACONIC_OBJECT(FontResource, ISerializable)
    public:
        String family;
        FontResourcePixels pixels = FontResourcePixels::Alpha8;
        Array<FontResourceEntry> entries;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "family", family);
            u32 mode = static_cast<u32>(pixels);
            draconic::core::Serialize(ar, "pixels", mode);
            pixels = static_cast<FontResourcePixels>(mode);
            draconic::core::Serialize(ar, "entries", entries);
        }
    };

    // Runtime product: rasterizer-free font + atlas + uploadable atlas image, per entry.
    class Font final : public Object
    {
        DRACONIC_OBJECT(Font, Object)
    public:
        struct Entry
        {
            f32 pixelHeight = 0.0f;
            UniquePtr<BakedFont> font;
            UniquePtr<IFontAtlas> atlas;        // BakedFontAtlas or DFFontAtlas
            UniquePtr<image::OwnedImageData> atlasImage; // RGBA8, renderer-uploadable
        };

        Font() = default;
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        void SetFamily(StringView family) { m_family = String(family); }
        void AddEntry(Entry&& entry) { m_entries.PushBack(Move(entry)); }

        [[nodiscard]] StringView Family() const noexcept { return m_family.AsView(); }
        [[nodiscard]] usize EntryCount() const noexcept { return m_entries.Size(); }
        [[nodiscard]] const Entry& EntryAt(usize index) const { return m_entries[index]; }

        // The entry whose pixelHeight is closest to `pixelHeight` (null when empty). The
        // P3 font service maps (family, size) requests through this.
        [[nodiscard]] const Entry* ClosestEntry(f32 pixelHeight) const
        {
            const Entry* best = nullptr;
            f32 bestDistance = 0.0f;
            for (const Entry& entry : m_entries)
            {
                const f32 distance = entry.pixelHeight > pixelHeight
                                         ? entry.pixelHeight - pixelHeight
                                         : pixelHeight - entry.pixelHeight;
                if (best == nullptr || distance < bestDistance)
                {
                    best = &entry;
                    bestDistance = distance;
                }
            }
            return best;
        }

    private:
        String m_family;
        Array<Entry> m_entries;
    };

    // Cooked FontResource -> runtime Font. Device-free: atlas images stay CPU-side
    // (image::OwnedImageData); the VG layer uploads them like any other font texture.
    class FontFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &Font::StaticType();
        }

        [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager,
                                            draconic::content::Instance& instance) override
        {
            (void)manager;
            RefPtr<ISerializable> object = instance.ReadObject();
            FontResource* res = Cast<FontResource>(object.Get());
            if (res == nullptr)
            {
                return RefPtr<Object>{};
            }

            // The concatenated atlas payloads.
            Array<u8> pixels;
            if (UniquePtr<IStream> stream = instance.ReadData(u8"data"))
            {
                const i64 size = stream->Size();
                if (size > 0)
                {
                    pixels.Resize(static_cast<usize>(size));
                    if (stream->Read(pixels.Data(), static_cast<u64>(size)) !=
                        static_cast<u64>(size))
                    {
                        pixels.Clear();
                    }
                }
            }

            RefPtr<Font> product = MakeRef<Font>(DefaultAllocator());
            product->SetFamily(res->family.AsView());

            for (const FontResourceEntry& e : res->entries)
            {
                Font::Entry entry;
                entry.pixelHeight = e.pixelHeight;

                // The rasterizer-free IFont.
                entry.font = MakeUnique<BakedFont>(DefaultAllocator());
                entry.font->SetFamilyName(res->family.AsView());
                entry.font->SetPixelHeight(e.pixelHeight);
                entry.font->SetMetrics(
                    FontMetrics(e.ascent, e.descent, e.lineGap, e.pixelHeight, e.scale));
                for (const FontResourceGlyph& g : e.glyphs)
                {
                    entry.font->SetGlyph(g.codepoint, g.info);
                }
                for (const FontResourceKerning& k : e.kerning)
                {
                    entry.font->SetKerning(k.first, k.second, k.amount);
                }

                // The entry's atlas pixel slice.
                Array<u8> slice;
                if (e.pixelBytes > 0 && e.pixelOffset + e.pixelBytes <= pixels.Size())
                {
                    slice.Resize(static_cast<usize>(e.pixelBytes));
                    MemCopy(slice.Data(), pixels.Data() + e.pixelOffset,
                            static_cast<usize>(e.pixelBytes));
                }

                if (res->pixels == FontResourcePixels::DistanceField)
                {
                    auto atlas = MakeUnique<DFFontAtlas>(DefaultAllocator());
                    atlas->SetPixelRange(e.dfPixelRange);
                    atlas->SetWhitePixelUV(e.whitePixelU, e.whitePixelV);
                    for (const FontResourceRegion& r : e.regions)
                    {
                        atlas->SetRegion(r.codepoint, r.region);
                    }
                    // RGBA8 MSDF channels, LINEAR (geometric data, not color).
                    entry.atlasImage = MakeUnique<image::OwnedImageData>(
                        DefaultAllocator(), e.atlasWidth, e.atlasHeight,
                        image::PixelFormat::RGBA8,
                        Span<const u8>(slice.Data(), slice.Size()),
                        image::ImageColorSpace::Linear);
                    atlas->SetPixels(e.atlasWidth, e.atlasHeight, Move(slice));
                    entry.atlas = Move(atlas); // converting move: DFFontAtlas -> IFontAtlas
                }
                else
                {
                    auto atlas = MakeUnique<BakedFontAtlas>(DefaultAllocator());
                    atlas->SetWhitePixelUV(e.whitePixelU, e.whitePixelV);
                    for (const FontResourceRegion& r : e.regions)
                    {
                        atlas->SetRegion(r.codepoint, r.region);
                    }
                    atlas->SetPixels(e.atlasWidth, e.atlasHeight, Move(slice));
                    // Same RGBA8 expansion the TTF service performs for coverage atlases.
                    entry.atlasImage = UniquePtr<image::OwnedImageData>(
                        FontAtlasTexture::ExpandR8ToRGBA8(atlas.Get()), DefaultAllocator());
                    entry.atlas = Move(atlas); // converting move: BakedFontAtlas -> IFontAtlas
                }

                if (!entry.atlasImage)
                {
                    continue; // a corrupt entry never produces a half-usable font size
                }
                product->AddEntry(Move(entry));
            }

            if (product->EntryCount() == 0)
            {
                return RefPtr<Object>{};
            }
            return product;
        }
    };

    inline void RegisterFontResource()
    {
        GlobalTypeRegistry().Register(FontResource::StaticType());
        RegisterSerializable<FontResource>();
    }

    DRACONIC_DEFINE_OBJECT(FontResource, "draconic::fonts")
    DRACONIC_DEFINE_OBJECT(Font, "draconic::fonts")
}
