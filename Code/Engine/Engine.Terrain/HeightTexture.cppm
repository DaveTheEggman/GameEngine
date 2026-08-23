// Engine::Terrain - the `:heighttexture` partition.
//
// PHASE B: the GPU height texture. The renderer's VS fetches per-vertex height via textureLoad, so
// each heightfield's u16 grid is uploaded to an R16Uint 2D texture (one texel per sample, exact).
// CACHED by the heightfield resource id + version: two terrains referencing one heightfield share
// ONE texture, and a version bump (a sculpt re-upload, phase 2) rebuilds it - the bind-group-cache
// versioning rule. Foundation::Heightfield stays a CPU grid (nav/physics pay no GPU); the texture is
// owned HERE.

module;
#include "Core/Prelude.h"

export module engine.terrain:heighttexture;

import foundation.core;
import foundation.rhi;
import foundation.heightfield;

using namespace foundation::core;

export namespace engine::terrain
{
    namespace rhi = foundation::rhi;
    namespace heightfield = foundation::heightfield;

    /// Owns + caches the per-heightfield GPU height textures (R16Uint), keyed by resource id+version.
    class TerrainHeightTextureCache
    {
    public:
        TerrainHeightTextureCache() = default;
        TerrainHeightTextureCache(const TerrainHeightTextureCache&) = delete;
        TerrainHeightTextureCache& operator=(const TerrainHeightTextureCache&) = delete;

        /// The R16Uint height texture view for `hf` at `version`. Keyed by the heightfield's
        /// UID (never its pointer - a freed grid's address can be reused by a fresh one at an
        /// equal version, and pointer keying would serve the dead grid's texture; the
        /// bind-group-cache versioning rule, pass-14 finding). Two terrains referencing one
        /// heightfield share ONE texture, and in-memory heightfields with no resource guid
        /// work (the uid is per-object, not per-resource). Creates + uploads + caches on a
        /// miss; a stale version for the same heightfield rebuilds in place (the sculpt
        /// re-upload path). Returns null on a device failure or an empty grid.
        [[nodiscard]] rhi::TextureView* GetOrCreate(rhi::Device& device,
                                                    const heightfield::Heightfield& hf, u64 version)
        {
            if (hf.IsEmpty())
            {
                return nullptr;
            }
            for (Entry& entry : m_entries)
            {
                if (entry.key == hf.uid)
                {
                    if (entry.version == version && entry.view != nullptr)
                    {
                        return entry.view;
                    }
                    Destroy(device, entry); // stale version: rebuild in place
                    if (!Build(device, hf, entry))
                    {
                        return nullptr;
                    }
                    entry.version = version;
                    return entry.view;
                }
            }
            Entry fresh;
            fresh.key = hf.uid;
            fresh.version = version;
            if (!Build(device, hf, fresh))
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
            u64 key = 0; // Heightfield::uid - never a pointer (address reuse aliases)
            u64 version = 0;
            rhi::Texture* texture = nullptr;
            rhi::TextureView* view = nullptr;
        };

        [[nodiscard]] static bool Build(rhi::Device& device, const heightfield::Heightfield& hf,
                                        Entry& out)
        {
            const u32 n = static_cast<u32>(hf.Size());
            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::R16Uint;
            td.width = n;
            td.height = n;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"terrain.height";
            if (!device.CreateTexture(td, out.texture).IsOk() || out.texture == nullptr)
            {
                return false;
            }
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::R16Uint;
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
                    const Span<const heightfield::Height> samples = hf.Samples();
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = n * static_cast<u32>(sizeof(heightfield::Height));
                    layout.rowsPerImage = n;
                    batch->WriteTexture(
                        out.texture,
                        Span<const u8>(reinterpret_cast<const u8*>(samples.Data()),
                                       samples.Size() * sizeof(heightfield::Height)),
                        layout, rhi::Extent3D{n, n, 1});
                    (void)batch->Submit();
                    queue->DestroyTransferBatch(batch);
                }
            }
            return true;
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
    };
}
