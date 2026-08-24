/// Foundation::Terrain.Resource - :splatmap partition.
///
/// The editable terrain SPLATMAP: an RGBA8 weight raster (one channel per layer 0..3) that is the
/// CPU SOURCE OF TRUTH for splat blending, exactly as foundation.heightfield::Heightfield is for
/// geometry. The GPU splat texture is DERIVED from it (engine.terrain's version-keyed cache re-
/// uploads on a bump) - so cooked and editor-painted terrains share ONE path. The pure brush core
/// (PaintWeight) lives here with the type (the SculptRaise precedent); no RHI, headless-testable.
///
/// SplatmapSource is the cooked metadata (width/height); the u8 pixel bulk rides a separate "pixels"
/// data stream (bulk-data sidecar rule). SplatmapFactory builds the runtime Splatmap.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.terrain.resource:splatmap;

import foundation.core;
import foundation.resource;
import foundation.content;

using namespace foundation::core;
using namespace foundation::resource;

export namespace foundation::terrain
{
    /// Number of blendable layers (RGBA channels). D2 caps at 4; a second splatmap is deferred.
    inline constexpr u32 kSplatLayerCount = 4;

    /// An RGBA8 layer-weight raster over the terrain's 0..1 footprint UV. IS-A Object so it is a
    /// referenceable product (Ref<Splatmap>) the terrain holds and physics/gameplay can query. The
    /// per-object uid + Version() drive the GPU cache invalidation (the Heightfield precedent).
    class Splatmap final : public Object
    {
        RTTI_OBJECT(Splatmap, Object)
    public:
        // Unique per-OBJECT id: GPU caches key by THIS + Version(), never by pointer - a freed
        // raster's address can be reused by a fresh one at an equal version (the height-texture
        // cache rule, bind-group-cache-versioning).
        const u64 uid = NextUid();

        [[nodiscard]] static u64 NextUid() noexcept
        {
            static Atomic<u64> counter{0};
            return counter.fetch_add(1) + 1;
        }

        Splatmap() = default;

        /// Allocate an all-zero raster (every weight 0 -> the shader's zero-sum guard renders layer
        /// 0, so an unseeded splatmap is a valid "all base layer" surface). SeedLayer0 makes that
        /// explicit for authoring.
        Splatmap(i32 width, i32 height) : m_width(width), m_height(height)
        {
            m_pixels.Resize(static_cast<usize>(width) * static_cast<usize>(height) * 4u, u8{0});
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_width <= 0 || m_height <= 0; }
        [[nodiscard]] i32 Width() const noexcept { return m_width; }
        [[nodiscard]] i32 Height() const noexcept { return m_height; }

        /// Monotonic edit generation (starts at 1). Bump after a paint so the GPU splat texture
        /// re-uploads (the sculpt/regen path).
        [[nodiscard]] u64 Version() const noexcept { return m_version; }
        void BumpVersion() noexcept { ++m_version; }

        [[nodiscard]] Span<const u8> Pixels() const noexcept
        {
            return Span<const u8>{m_pixels.Data(), m_pixels.Size()};
        }
        [[nodiscard]] Span<u8> Pixels() noexcept
        {
            return Span<u8>{m_pixels.Data(), m_pixels.Size()};
        }

        /// One channel weight (0..255) at a texel (indices clamped).
        [[nodiscard]] u8 GetWeight(i32 x, i32 y, u32 layer) const noexcept
        {
            return m_pixels[Index(x, y) + (layer & 3u)];
        }

        /// Fill the whole raster with layer 0 at full weight (255,0,0,0) - the authoring seed for a
        /// freshly created splatmap so the base layer shows before any painting.
        void SeedLayer0() noexcept
        {
            for (usize i = 0; i + 3 < m_pixels.Size(); i += 4)
            {
                m_pixels[i] = 255;
                m_pixels[i + 1] = 0;
                m_pixels[i + 2] = 0;
                m_pixels[i + 3] = 0;
            }
        }

    private:
        [[nodiscard]] usize Index(i32 x, i32 y) const noexcept
        {
            const i32 cx = x < 0 ? 0 : (x >= m_width ? m_width - 1 : x);
            const i32 cy = y < 0 ? 0 : (y >= m_height ? m_height - 1 : y);
            return (static_cast<usize>(cy) * static_cast<usize>(m_width) + static_cast<usize>(cx)) *
                   4u;
        }

        i32 m_width = 0;
        i32 m_height = 0;
        u64 m_version = 1; // edit generation (BumpVersion) for GPU-cache invalidation
        Array<u8> m_pixels; // RGBA8, row-major, index = (x + y*w)*4
    };

    // ---- splat weight brush (pure texel math; the editor splat tool wraps this) -----------------
    //
    // Paints a WORLD/UV-space disc (centre uvX,uvY in the 0..1 footprint; uvRadius in the same
    // units) into ONE layer channel with a cosine falloff, LERP-TO-ONE-HOT so the painted layer
    // takes over and the weight vector stays bounded (Fable ruling Q5): t = amount*falloff,
    // w_sel += t*(1 - w_sel), w_other *= (1 - t) - painting another layer therefore erases this one.
    // BumpVersion()s if any texel changed, returns the touched pixel RECT for region-delta undo +
    // the bounded GPU re-upload. All headless-testable.

    /// The pixel rectangle a paint touched (inclusive). Empty when nothing was in range.
    struct SplatRegion
    {
        i32 minX = 0x7fffffff;
        i32 minY = 0x7fffffff;
        i32 maxX = -1;
        i32 maxY = -1;

        [[nodiscard]] bool IsEmpty() const noexcept { return maxX < minX || maxY < minY; }
        [[nodiscard]] i32 Width() const noexcept { return IsEmpty() ? 0 : (maxX - minX + 1); }
        [[nodiscard]] i32 Height() const noexcept { return IsEmpty() ? 0 : (maxY - minY + 1); }
        void Add(i32 x, i32 y) noexcept
        {
            minX = x < minX ? x : minX;
            minY = y < minY ? y : minY;
            maxX = x > maxX ? x : maxX;
            maxY = y > maxY ? y : maxY;
        }
    };

    /// Paint `layer` (0..3) into the splatmap over a disc centred at UV (uvX,uvY), radius uvRadius
    /// (both in 0..1 footprint units), strength `amount` in [0,1]. Lerp-to-one-hot per texel.
    inline SplatRegion PaintWeight(Splatmap& sm, f32 uvX, f32 uvY, f32 uvRadius, u32 layer, f32 amount)
    {
        SplatRegion region;
        if (sm.IsEmpty() || uvRadius <= 0.0f || amount <= 0.0f)
        {
            return region;
        }
        const i32 w = sm.Width();
        const i32 h = sm.Height();
        // Texel centres sit at (i+0.5)/dim; work in continuous pixel space.
        const f32 cx = uvX * static_cast<f32>(w);
        const f32 cy = uvY * static_cast<f32>(h);
        const f32 rx = uvRadius * static_cast<f32>(w);
        const f32 ry = uvRadius * static_cast<f32>(h);
        const i32 x0 = Max(0, static_cast<i32>(Floor(cx - rx)));
        const i32 x1 = Min(w - 1, static_cast<i32>(Ceil(cx + rx)));
        const i32 y0 = Max(0, static_cast<i32>(Floor(cy - ry)));
        const i32 y1 = Min(h - 1, static_cast<i32>(Ceil(cy + ry)));
        const f32 invR = 1.0f / uvRadius;
        const u32 sel = layer & 3u;
        Span<u8> px = sm.Pixels();
        bool changed = false;
        for (i32 y = y0; y <= y1; ++y)
        {
            for (i32 x = x0; x <= x1; ++x)
            {
                // Distance in UV units (normalize the pixel delta by the raster dims).
                const f32 du = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(w) - uvX;
                const f32 dv = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(h) - uvY;
                const f32 dist = Sqrt(du * du + dv * dv);
                if (dist >= uvRadius)
                {
                    continue;
                }
                const f32 fall = 0.5f + 0.5f * Cos(3.14159265f * dist * invR); // 1 centre..0 rim
                const f32 t = Clamp(amount * fall, 0.0f, 1.0f);
                if (t <= 0.0f)
                {
                    continue;
                }
                const usize base = (static_cast<usize>(y) * static_cast<usize>(w) +
                                    static_cast<usize>(x)) *
                                   4u;
                f32 c[4];
                for (u32 k = 0; k < 4; ++k)
                {
                    c[k] = static_cast<f32>(px[base + k]) * (1.0f / 255.0f);
                }
                c[sel] = c[sel] + t * (1.0f - c[sel]);
                for (u32 k = 0; k < 4; ++k)
                {
                    if (k != sel)
                    {
                        c[k] *= (1.0f - t);
                    }
                }
                bool texelChanged = false;
                for (u32 k = 0; k < 4; ++k)
                {
                    const u8 q = static_cast<u8>(Clamp(c[k] * 255.0f + 0.5f, 0.0f, 255.0f));
                    if (q != px[base + k])
                    {
                        px[base + k] = q;
                        texelChanged = true;
                    }
                }
                if (texelChanged)
                {
                    region.Add(x, y);
                    changed = true;
                }
            }
        }
        if (changed)
        {
            sm.BumpVersion();
        }
        return region;
    }

    /// Cooked splatmap METADATA: just the raster dimensions. The RGBA8 pixel bulk is NOT here - it
    /// rides the `kSplatStream` data stream (the ImageResource "pixels" precedent).
    class SplatmapSource final : public ISerializable
    {
        RTTI_OBJECT(SplatmapSource, ISerializable)
    public:
        i32 width = 0;
        i32 height = 0;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "width", width);
            foundation::core::Serialize(ar, "height", height);
        }

        /// Capture a runtime Splatmap's METADATA (for cooking). Pixels are written separately via
        /// PixelBlob + WriteData(kSplatStream, ...).
        static void FromSplatmap(const Splatmap& sm, SplatmapSource& out)
        {
            out.width = sm.Width();
            out.height = sm.Height();
        }

        /// The raw RGBA8 bytes of a splatmap, to feed WriteData(kSplatStream, ...).
        [[nodiscard]] static Span<const u8> PixelBlob(const Splatmap& sm) noexcept
        {
            return sm.Pixels();
        }

        /// Build the runtime product from this metadata + the sidecar pixel bytes. Returns an empty
        /// raster if the cooked data is inconsistent (non-positive dims, or a blob that does not
        /// match width*height*4) rather than a malformed raster.
        [[nodiscard]] RefPtr<Splatmap> Build(Span<const u8> blob) const
        {
            if (width <= 0 || height <= 0)
            {
                return MakeRef<Splatmap>(DefaultAllocator());
            }
            const usize expected =
                static_cast<usize>(width) * static_cast<usize>(height) * 4u;
            if (blob.Size() != expected)
            {
                return MakeRef<Splatmap>(DefaultAllocator());
            }
            RefPtr<Splatmap> sm = MakeRef<Splatmap>(DefaultAllocator(), width, height);
            MemCopy(sm->Pixels().Data(), blob.Data(), expected);
            return sm;
        }
    };

    /// The name of the sidecar stream carrying the raw RGBA8 pixels.
    inline constexpr StringView kSplatStream = u8"pixels";

    /// Builds a cooked splatmap (metadata object + "pixels" stream) into a runtime Splatmap. Pure-CPU
    /// (engine.terrain owns the GPU texture, cached per resource), so the whole build runs on a worker.
    class SplatmapFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &Splatmap::StaticType(); }
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
            SplatmapSource* src = Cast<SplatmapSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            Array<u8> blob;
            if (UniquePtr<IStream> stream = instance.ReadData(kSplatStream))
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
            return src->Build(Span<const u8>{blob.Data(), blob.Size()});
        }
    };

    /// Register the splatmap resource types (product + cooked source) for load.
    inline void RegisterSplatmapResourceTypes()
    {
        GlobalTypeRegistry().Register(Splatmap::StaticType());
        GlobalTypeRegistry().Register(SplatmapSource::StaticType());
        RegisterSerializable<SplatmapSource>();
    }

    RTTI_DEFINE_OBJECT(Splatmap, "rtti::terrain")
    RTTI_DEFINE_OBJECT_VERSIONED(SplatmapSource, "rtti::terrain", 1)
}
