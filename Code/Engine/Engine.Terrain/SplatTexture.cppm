// Engine::Terrain - the `:splattexture` partition.
//
// The GPU splat textures for the top-K blend (terrain-splat-topk.md): the CPU SplatWeights (the
// painted source of truth) derives TWO textures per resource -
//   - weights: RGBA8Unorm (the 4 slot weights; filterable, though the top-K PS Loads it for the
//     manual bilinear),
//   - indices: RGBA8Uint (the 4 palette indices; INTEGER + non-filterable BY DESIGN - filtering
//     indices interpolates layer ids into garbage, ruling R1; Load-only in the shader).
// CACHED by the resource UID + version, exactly like the height-texture cache: two terrains
// sharing one weights raster share ONE texture pair, and a paint (a version bump) RETIRES the old
// pair and re-uploads - the new view ids make the set-3 bind cache rebuild for free (the live-
// repaint path). Single-mip both.

module;
#include "Core/Prelude.h"

export module engine.terrain:splattexture;

import foundation.core;
import foundation.rhi;
import foundation.render;           // GpuRetireQueue (in-flight-safe destruction)
import foundation.terrain.resource; // SplatWeights (the CPU top-K rasters)

using namespace foundation::core;

export namespace engine::terrain
{
    namespace rhi = foundation::rhi;
    namespace render = foundation::render;

    /// The derived GPU views for one SplatWeights resource (null when unresolved).
    struct SplatTextureViews
    {
        rhi::TextureView* weightView = nullptr; // RGBA8Unorm
        rhi::TextureView* indexView = nullptr;  // RGBA8Uint (Load-only)
    };

    /// Owns + caches the per-resource GPU texture pairs, keyed by resource UID + version.
    class TerrainSplatTextureCache
    {
    public:
        TerrainSplatTextureCache() = default;
        TerrainSplatTextureCache(const TerrainSplatTextureCache&) = delete;
        TerrainSplatTextureCache& operator=(const TerrainSplatTextureCache&) = delete;

        /// In-flight-safe destruction (wired by the subsystem): a paint's version-bump rebuild
        /// RETIRES the old textures/views through the frame-aged queue - a submitted frame still
        /// samples the old views through the renderer's set-3 bind group. Unwired (headless / Null
        /// device) falls back to direct destroy.
        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept { m_retire = retire; }

        /// The texture pair for `sw` at `version`. Keyed by the resource's UID (never its pointer
        /// - a freed raster's address can be reused by a fresh one at an equal version; the
        /// bind-group-cache versioning rule). Creates + uploads + caches on a miss; a stale
        /// version for the same resource rebuilds in place (the paint re-upload path). Null views
        /// on a device failure or an empty raster.
        [[nodiscard]] SplatTextureViews GetOrCreate(rhi::Device& device,
                                                    const foundation::terrain::SplatWeights& sw,
                                                    u64 version)
        {
            if (sw.IsEmpty())
            {
                return SplatTextureViews{};
            }
            for (Entry& entry : m_entries)
            {
                if (entry.key == sw.uid)
                {
                    if (entry.version == version && entry.weightView != nullptr)
                    {
                        return SplatTextureViews{entry.weightView, entry.indexView};
                    }
                    RetireOrDestroy(device, entry); // stale version: rebuild in place
                    if (!Build(device, sw, entry))
                    {
                        return SplatTextureViews{};
                    }
                    entry.version = version;
                    return SplatTextureViews{entry.weightView, entry.indexView};
                }
            }
            Entry fresh;
            fresh.key = sw.uid;
            fresh.version = version;
            if (!Build(device, sw, fresh))
            {
                return SplatTextureViews{};
            }
            m_entries.PushBack(fresh);
            const Entry& stored = m_entries[m_entries.Size() - 1];
            return SplatTextureViews{stored.weightView, stored.indexView};
        }

        /// Destroy every cached texture/view (call before the device dies).
        void Clear(rhi::Device& device)
        {
            for (Entry& entry : m_entries)
            {
                Destroy(device, entry);
            }
            m_entries.Clear();
        }

        [[nodiscard]] usize Size() const noexcept { return m_entries.Size(); }

    private:
        struct Entry
        {
            u64 key = 0; // SplatWeights::uid - never a pointer (address reuse aliases)
            u64 version = 0;
            rhi::Texture* weightTexture = nullptr;
            rhi::TextureView* weightView = nullptr;
            rhi::Texture* indexTexture = nullptr;
            rhi::TextureView* indexView = nullptr;
        };

        [[nodiscard]] static bool BuildOne(rhi::Device& device, u32 w, u32 h,
                                           rhi::TextureFormat format, Span<const u8> pixels,
                                           StringView label, rhi::Texture*& outTexture,
                                           rhi::TextureView*& outView)
        {
            rhi::TextureDesc td{};
            td.format = format;
            td.width = w;
            td.height = h;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = label;
            if (!device.CreateTexture(td, outTexture).IsOk() || outTexture == nullptr)
            {
                return false;
            }
            rhi::TextureViewDesc vd{};
            vd.format = format;
            vd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!device.CreateTextureView(outTexture, vd, outView).IsOk() || outView == nullptr)
            {
                device.DestroyTexture(outTexture);
                outTexture = nullptr;
                return false;
            }
            if (rhi::Queue* queue = device.GetQueue(rhi::QueueType::Graphics))
            {
                rhi::TransferBatch* batch = nullptr;
                if (queue->CreateTransferBatch(batch).IsOk() && batch != nullptr)
                {
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = w * 4u;
                    layout.rowsPerImage = h;
                    batch->WriteTexture(outTexture, pixels, layout, rhi::Extent3D{w, h, 1});
                    (void)batch->Submit();
                    queue->DestroyTransferBatch(batch);
                }
            }
            return true;
        }

        [[nodiscard]] static bool Build(rhi::Device& device,
                                        const foundation::terrain::SplatWeights& sw, Entry& out)
        {
            const u32 w = static_cast<u32>(sw.Width());
            const u32 h = static_cast<u32>(sw.Height());
            if (!BuildOne(device, w, h, rhi::TextureFormat::RGBA8Unorm, sw.Weights(),
                          u8"terrain.splat.weights", out.weightTexture, out.weightView))
            {
                return false;
            }
            if (!BuildOne(device, w, h, rhi::TextureFormat::RGBA8Uint, sw.Indices(),
                          u8"terrain.splat.indices", out.indexTexture, out.indexView))
            {
                device.DestroyTextureView(out.weightView);
                device.DestroyTexture(out.weightTexture);
                out.weightView = nullptr;
                out.weightTexture = nullptr;
                return false;
            }
            return true;
        }

        void RetireOrDestroy(rhi::Device& device, Entry& entry)
        {
            if (m_retire != nullptr)
            {
                m_retire->Retire(entry.weightView); // aged past every in-flight frame, then freed
                m_retire->Retire(entry.weightTexture);
                m_retire->Retire(entry.indexView);
                m_retire->Retire(entry.indexTexture);
                entry.weightView = nullptr;
                entry.weightTexture = nullptr;
                entry.indexView = nullptr;
                entry.indexTexture = nullptr;
                return;
            }
            Destroy(device, entry);
        }

        static void Destroy(rhi::Device& device, Entry& entry)
        {
            if (entry.weightView != nullptr)
            {
                device.DestroyTextureView(entry.weightView);
                entry.weightView = nullptr;
            }
            if (entry.weightTexture != nullptr)
            {
                device.DestroyTexture(entry.weightTexture);
                entry.weightTexture = nullptr;
            }
            if (entry.indexView != nullptr)
            {
                device.DestroyTextureView(entry.indexView);
                entry.indexView = nullptr;
            }
            if (entry.indexTexture != nullptr)
            {
                device.DestroyTexture(entry.indexTexture);
                entry.indexTexture = nullptr;
            }
        }

        Array<Entry> m_entries;
        render::GpuRetireQueue* m_retire = nullptr; // borrowed (RenderSubsystem owns + ticks it)
    };

    /// The palette-array GPU views + the per-layer tileScale buffer for one terrain.
    struct PaletteGpu
    {
        rhi::TextureView* arrayView = nullptr; // Texture2DArray, one slice per palette layer
        rhi::Buffer* tileScaleBuffer = nullptr; // f32[paletteCount]; base tile rides the view UBO
        u64 generation = 0;                     // part of the set-3 cache key
    };

    /// Owns + caches the palette Texture2DArray + tileScale storage buffer per terrain resource,
    /// keyed by TerrainPaletteData::uid + a tile-scale hash (never pointers). A palette edit
    /// re-cooks the terrain -> a NEW TerrainPaletteData (new uid) -> rebuild; the OLD array is
    /// RETIRED (in-flight frames still sample it through the old set-3 group).
    class TerrainPaletteTextureCache
    {
    public:
        TerrainPaletteTextureCache() = default;
        TerrainPaletteTextureCache(const TerrainPaletteTextureCache&) = delete;
        TerrainPaletteTextureCache& operator=(const TerrainPaletteTextureCache&) = delete;

        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept { m_retire = retire; }

        [[nodiscard]] PaletteGpu
        GetOrCreate(rhi::Device& device, const foundation::terrain::TerrainPaletteData& data,
                    Span<const f32> paletteTileScales)
        {
            if (!data.IsValid())
            {
                return PaletteGpu{};
            }
            const u64 scaleHash = HashScales(paletteTileScales);
            for (Entry& entry : m_entries)
            {
                if (entry.key == data.uid)
                {
                    if (entry.scaleHash == scaleHash && entry.arrayView != nullptr)
                    {
                        return PaletteGpu{entry.arrayView, entry.tileScaleBuffer,
                                          entry.generation};
                    }
                    RetireOrDestroy(device, entry);
                    if (!Build(device, data, paletteTileScales, entry))
                    {
                        return PaletteGpu{};
                    }
                    entry.scaleHash = scaleHash;
                    ++entry.generation;
                    return PaletteGpu{entry.arrayView, entry.tileScaleBuffer, entry.generation};
                }
            }
            Entry fresh;
            fresh.key = data.uid;
            fresh.scaleHash = scaleHash;
            fresh.generation = 1;
            if (!Build(device, data, paletteTileScales, fresh))
            {
                return PaletteGpu{};
            }
            m_entries.PushBack(fresh);
            const Entry& stored = m_entries[m_entries.Size() - 1];
            return PaletteGpu{stored.arrayView, stored.tileScaleBuffer, stored.generation};
        }

        void Clear(rhi::Device& device)
        {
            for (Entry& entry : m_entries)
            {
                Destroy(device, entry);
            }
            m_entries.Clear();
        }

        [[nodiscard]] usize Size() const noexcept { return m_entries.Size(); }

    private:
        struct Entry
        {
            u64 key = 0; // TerrainPaletteData::uid - never a pointer
            u64 scaleHash = 0;
            u64 generation = 0;
            rhi::Texture* arrayTexture = nullptr;
            rhi::TextureView* arrayView = nullptr;
            rhi::Buffer* tileScaleBuffer = nullptr;
        };

        [[nodiscard]] static u64 HashScales(Span<const f32> scales) noexcept
        {
            u64 h = 1469598103934665603ull;
            const auto mix = [&h](f32 v)
            {
                u32 bits;
                MemCopy(&bits, &v, sizeof(bits));
                h = (h ^ bits) * 1099511628211ull;
            };
            for (usize i = 0; i < scales.Size(); ++i)
            {
                mix(scales[i]);
            }
            return h;
        }

        [[nodiscard]] static bool Build(rhi::Device& device,
                                        const foundation::terrain::TerrainPaletteData& data,
                                        Span<const f32> paletteTileScales, Entry& out)
        {
            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8Unorm;
            td.width = data.sliceSize;
            td.height = data.sliceSize;
            td.arrayLayerCount = data.sliceCount;
            td.mipLevelCount = data.mipCount;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"terrain.palette";
            if (!device.CreateTexture(td, out.arrayTexture).IsOk() || out.arrayTexture == nullptr)
            {
                return false;
            }
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::RGBA8Unorm;
            vd.dimension = rhi::TextureViewDimension::Texture2DArray;
            vd.arrayLayerCount = data.sliceCount;
            vd.mipLevelCount = data.mipCount;
            if (!device.CreateTextureView(out.arrayTexture, vd, out.arrayView).IsOk() ||
                out.arrayView == nullptr)
            {
                device.DestroyTexture(out.arrayTexture);
                out.arrayTexture = nullptr;
                return false;
            }

            if (rhi::Queue* queue = device.GetQueue(rhi::QueueType::Graphics))
            {
                rhi::TransferBatch* batch = nullptr;
                if (queue->CreateTransferBatch(batch).IsOk() && batch != nullptr)
                {
                    const usize sliceBytes = foundation::terrain::TerrainPaletteData::SliceBytes(
                        data.sliceSize, data.mipCount);
                    for (u32 slice = 0; slice < data.sliceCount; ++slice)
                    {
                        const u8* sliceBase = data.texels.Data() + sliceBytes * slice;
                        usize offset = 0;
                        u32 dim = data.sliceSize;
                        for (u32 m = 0; m < data.mipCount; ++m)
                        {
                            const usize bytes = static_cast<usize>(dim) * dim * 4u;
                            rhi::TextureDataLayout layout{};
                            layout.bytesPerRow = dim * 4u;
                            layout.rowsPerImage = dim;
                            batch->WriteTexture(out.arrayTexture,
                                                Span<const u8>{sliceBase + offset, bytes}, layout,
                                                rhi::Extent3D{dim, dim, 1}, m, slice);
                            offset += bytes;
                            dim = dim > 1 ? dim / 2 : 1;
                        }
                    }
                    (void)batch->Submit();
                    queue->DestroyTransferBatch(batch);
                }
            }

            // The tileScale storage buffer: TileScales[i] = palette layer i (tight f32; the base
            // tile rides the view UBO, so a base-tile edit never rebuilds this buffer).
            Array<f32> scales;
            for (u32 i = 0; i < data.sliceCount; ++i)
            {
                scales.PushBack(i < paletteTileScales.Size() ? paletteTileScales[i] : 1.0f);
            }
            rhi::BufferDesc bd{};
            bd.size = scales.Size() * sizeof(f32);
            bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
            bd.memory = rhi::MemoryLocation::CpuToGpu;
            bd.label = u8"terrain.tileScales";
            if (!device.CreateBuffer(bd, out.tileScaleBuffer).IsOk() ||
                out.tileScaleBuffer == nullptr)
            {
                Destroy(device, out);
                return false;
            }
            if (void* mapped = out.tileScaleBuffer->Map())
            {
                MemCopy(mapped, scales.Data(), scales.Size() * sizeof(f32));
                out.tileScaleBuffer->Unmap();
            }
            return true;
        }

        void RetireOrDestroy(rhi::Device& device, Entry& entry)
        {
            if (m_retire != nullptr)
            {
                m_retire->Retire(entry.arrayView);
                m_retire->Retire(entry.arrayTexture);
                m_retire->Retire(entry.tileScaleBuffer);
                entry.arrayView = nullptr;
                entry.arrayTexture = nullptr;
                entry.tileScaleBuffer = nullptr;
                return;
            }
            Destroy(device, entry);
        }

        static void Destroy(rhi::Device& device, Entry& entry)
        {
            if (entry.arrayView != nullptr)
            {
                device.DestroyTextureView(entry.arrayView);
                entry.arrayView = nullptr;
            }
            if (entry.arrayTexture != nullptr)
            {
                device.DestroyTexture(entry.arrayTexture);
                entry.arrayTexture = nullptr;
            }
            if (entry.tileScaleBuffer != nullptr)
            {
                device.DestroyBuffer(entry.tileScaleBuffer);
                entry.tileScaleBuffer = nullptr;
            }
        }

        Array<Entry> m_entries;
        render::GpuRetireQueue* m_retire = nullptr; // borrowed (RenderSubsystem owns + ticks it)
    };
}
