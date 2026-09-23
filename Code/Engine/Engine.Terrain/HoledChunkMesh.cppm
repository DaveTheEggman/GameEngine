// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The holed-chunk index buffers, cached per heightfield (Specs/terrain-holes.md): a chunk whose
// sample block holds a cut sample cannot draw the shared grid, so it gets its own index buffer
// per LOD, built by foundation.terrain's BuildHoledChunkIndices and rebuilt in place when the
// heightfield's version moves (a cut, a fill, a sculpt). Keyed by the heightfield's uid, never a
// pointer; buffers retire through the frame-aged queue like the height texture's.

module;
#include "Core/Prelude.h"

export module engine.terrain:holedmesh;

import foundation.core;
import foundation.rhi;
import foundation.render; // GpuRetireQueue
import foundation.heightfield;
import foundation.terrain;
import :renderdata; // HoledChunkMesh

using namespace foundation::core;

export namespace engine::terrain
{
    namespace tmodel = foundation::terrain;

    class TerrainHoledMeshCache
    {
    public:
        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept { m_retire = retire; }

        /// The holed chunks' meshes for `hf` at `version` (empty when it has no holes): built on a
        /// miss or a stale version, else the cached set. The span is the cache's own storage;
        /// extraction copies it into the frame arena.
        [[nodiscard]] Span<const HoledChunkMesh> GetOrBuild(rhi::Device& device,
                                                            const foundation::heightfield::Heightfield& hf,
                                                            Span<const tmodel::TerrainChunk> chunks,
                                                            u64 version)
        {
            for (Entry& entry : m_entries)
            {
                if (entry.key != hf.uid)
                {
                    continue;
                }
                if (entry.version != version)
                {
                    Release(device, entry);
                    Build(device, hf, chunks, entry);
                    entry.version = version;
                }
                return Span<const HoledChunkMesh>{entry.meshes.Data(), entry.meshes.Size()};
            }
            if (!hf.HasHoles())
            {
                return {}; // the common terrain: no entry, no allocation
            }
            Entry fresh;
            fresh.key = hf.uid;
            fresh.version = version;
            Build(device, hf, chunks, fresh);
            m_entries.PushBack(Move(fresh));
            const Entry& stored = m_entries[m_entries.Size() - 1];
            return Span<const HoledChunkMesh>{stored.meshes.Data(), stored.meshes.Size()};
        }

        /// Drop every entry (scene destroy, shutdown): buffers retire when a queue is wired.
        void Clear(rhi::Device& device)
        {
            for (Entry& entry : m_entries)
            {
                Release(device, entry);
            }
            m_entries.Clear();
        }

        [[nodiscard]] usize Size() const noexcept { return m_entries.Size(); }
        /// Holed chunk records held for `uid` (0 = none): the test observable.
        [[nodiscard]] usize MeshCount(u64 uid) const noexcept
        {
            for (const Entry& entry : m_entries)
            {
                if (entry.key == uid)
                {
                    return entry.meshes.Size();
                }
            }
            return 0;
        }

    private:
        struct Entry
        {
            u64 key = 0;
            u64 version = 0;
            Array<HoledChunkMesh> meshes;
        };

        void Build(rhi::Device& device, const foundation::heightfield::Heightfield& hf,
                   Span<const tmodel::TerrainChunk> chunks, Entry& entry)
        {
            entry.meshes.Clear();
            Array<u32> indices;
            for (usize i = 0; i < chunks.Size(); ++i)
            {
                const tmodel::TerrainChunk& chunk = chunks[i];
                if (!chunk.hasHoles || chunk.allCut)
                {
                    continue; // the shared grid, or nothing at all
                }
                HoledChunkMesh mesh;
                mesh.chunkIndex = static_cast<u32>(i);
                for (u32 lod = 0; lod <= tmodel::kMaxChunkLod; ++lod)
                {
                    u32 surface = 0;
                    tmodel::BuildHoledChunkIndices(hf, chunk.gridX0, chunk.gridZ0, lod, indices, surface);
                    mesh.indexCounts[lod] = static_cast<u32>(indices.Size());
                    mesh.surfaceIndexCounts[lod] = surface;
                    if (indices.IsEmpty())
                    {
                        continue; // this LOD's quads all went: no buffer, no draw
                    }
                    rhi::BufferDesc ibd{};
                    ibd.size = indices.Size() * sizeof(u32);
                    ibd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
                    ibd.memory = rhi::MemoryLocation::CpuToGpu;
                    ibd.label = u8"terrain.holed.indices";
                    rhi::Buffer* buffer = nullptr;
                    if (!device.CreateBuffer(ibd, buffer).IsOk())
                    {
                        mesh.indexCounts[lod] = 0;
                        continue;
                    }
                    if (void* p = buffer->Map())
                    {
                        MemCopy(p, indices.Data(), indices.Size() * sizeof(u32));
                        buffer->Unmap();
                    }
                    mesh.indexBuffers[lod] = buffer;
                }
                entry.meshes.PushBack(mesh);
            }
        }

        void Release(rhi::Device& device, Entry& entry)
        {
            for (HoledChunkMesh& mesh : entry.meshes)
            {
                for (u32 lod = 0; lod <= tmodel::kMaxChunkLod; ++lod)
                {
                    if (mesh.indexBuffers[lod] == nullptr)
                    {
                        continue;
                    }
                    if (m_retire != nullptr)
                    {
                        m_retire->Retire(mesh.indexBuffers[lod]);
                    }
                    else
                    {
                        device.DestroyBuffer(mesh.indexBuffers[lod]);
                    }
                    mesh.indexBuffers[lod] = nullptr;
                }
            }
            entry.meshes.Clear();
        }

        Array<Entry> m_entries;
        render::GpuRetireQueue* m_retire = nullptr;
    };
}
