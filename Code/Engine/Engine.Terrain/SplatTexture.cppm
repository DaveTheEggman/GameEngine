// Engine::Terrain - the `:splattexture` partition.
//
// The GPU splat texture. The splatmap is a CPU RGBA8 weight raster (foundation::terrain::Splatmap,
// the source of truth painted by the editor); the renderer's set-3 material bind group samples an
// RGBA8Unorm 2D texture derived from it. CACHED by the splatmap resource UID + version, exactly like
// the height-texture cache: two terrains sharing one splatmap share ONE texture, and a paint (a
// version bump) RETIRES the old texture/view and re-uploads - the new view id makes the set-3 bind
// cache rebuild for free (the live-repaint path). Single-mip: the splat sampler is bilinear/mip-
// Nearest, so no mip chain is needed (matches the shipped D2 path exactly).

module;
#include "Core/Prelude.h"

export module engine.terrain:splattexture;

import foundation.core;
import foundation.rhi;
import foundation.render;          // GpuRetireQueue (in-flight-safe destruction)
import foundation.terrain.resource; // Splatmap (the CPU weight raster)

using namespace foundation::core;

export namespace engine::terrain
{
    namespace rhi = foundation::rhi;
    namespace render = foundation::render;

    /// Owns + caches the per-splatmap GPU textures (RGBA8Unorm), keyed by resource UID + version.
    class TerrainSplatTextureCache
    {
    public:
        TerrainSplatTextureCache() = default;
        TerrainSplatTextureCache(const TerrainSplatTextureCache&) = delete;
        TerrainSplatTextureCache& operator=(const TerrainSplatTextureCache&) = delete;

        /// In-flight-safe destruction (wired by the subsystem): a paint's version-bump rebuild
        /// RETIRES the old texture/view through the frame-aged queue - a submitted frame still
        /// samples the old view through the renderer's set-3 bind group. Unwired (headless / Null
        /// device) falls back to direct destroy.
        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept { m_retire = retire; }

        /// The RGBA8Unorm splat texture view for `sm` at `version`. Keyed by the splatmap's UID
        /// (never its pointer - a freed raster's address can be reused by a fresh one at an equal
        /// version; the bind-group-cache versioning rule). Creates + uploads + caches on a miss; a
        /// stale version for the same splatmap rebuilds in place (the paint re-upload path).
        /// Returns null on a device failure or an empty raster.
        [[nodiscard]] rhi::TextureView* GetOrCreate(rhi::Device& device,
                                                    const foundation::terrain::Splatmap& sm,
                                                    u64 version)
        {
            if (sm.IsEmpty())
            {
                return nullptr;
            }
            for (Entry& entry : m_entries)
            {
                if (entry.key == sm.uid)
                {
                    if (entry.version == version && entry.view != nullptr)
                    {
                        return entry.view;
                    }
                    RetireOrDestroy(device, entry); // stale version: rebuild in place
                    if (!Build(device, sm, entry))
                    {
                        return nullptr;
                    }
                    entry.version = version;
                    return entry.view;
                }
            }
            Entry fresh;
            fresh.key = sm.uid;
            fresh.version = version;
            if (!Build(device, sm, fresh))
            {
                return nullptr;
            }
            m_entries.PushBack(fresh);
            return m_entries[m_entries.Size() - 1].view;
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
            u64 key = 0; // Splatmap::uid - never a pointer (address reuse aliases)
            u64 version = 0;
            rhi::Texture* texture = nullptr;
            rhi::TextureView* view = nullptr;
        };

        [[nodiscard]] static bool Build(rhi::Device& device,
                                        const foundation::terrain::Splatmap& sm, Entry& out)
        {
            const u32 w = static_cast<u32>(sm.Width());
            const u32 h = static_cast<u32>(sm.Height());
            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8Unorm; // weights are DATA (not sRGB)
            td.width = w;
            td.height = h;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"terrain.splat";
            if (!device.CreateTexture(td, out.texture).IsOk() || out.texture == nullptr)
            {
                return false;
            }
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::RGBA8Unorm;
            vd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!device.CreateTextureView(out.texture, vd, out.view).IsOk() || out.view == nullptr)
            {
                device.DestroyTexture(out.texture);
                return false;
            }
            if (rhi::Queue* queue = device.GetQueue(rhi::QueueType::Graphics))
            {
                rhi::TransferBatch* batch = nullptr;
                if (queue->CreateTransferBatch(batch).IsOk() && batch != nullptr)
                {
                    const Span<const u8> px = sm.Pixels();
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = w * 4u;
                    layout.rowsPerImage = h;
                    batch->WriteTexture(out.texture, px, layout, rhi::Extent3D{w, h, 1});
                    (void)batch->Submit();
                    queue->DestroyTransferBatch(batch);
                }
            }
            return true;
        }

        void RetireOrDestroy(rhi::Device& device, Entry& entry)
        {
            if (m_retire != nullptr)
            {
                m_retire->Retire(entry.view); // aged past every in-flight frame, then freed
                m_retire->Retire(entry.texture);
                entry.view = nullptr;
                entry.texture = nullptr;
                return;
            }
            Destroy(device, entry);
        }

        static void Destroy(rhi::Device& device, Entry& entry)
        {
            if (entry.view != nullptr)
            {
                device.DestroyTextureView(entry.view);
                entry.view = nullptr;
            }
            if (entry.texture != nullptr)
            {
                device.DestroyTexture(entry.texture);
                entry.texture = nullptr;
            }
        }

        Array<Entry> m_entries;
        render::GpuRetireQueue* m_retire = nullptr; // borrowed (RenderSubsystem owns + ticks it)
    };
}
