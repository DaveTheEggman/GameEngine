/// Raptor::Render — the `:mesh_gpu` partition.
///
/// MeshGpuCache: uploads a StaticMesh's vertex + index streams to the GPU on first use and
/// caches them by mesh pointer (so repeated draws reuse the buffers). The streams are
/// sub-allocated from shared vertex/index pools (§8 — no per-mesh buffers); a mesh records
/// the pool buffer + its byte offset, and draws bind with that offset. Part of the
/// scene-agnostic renderer — it consumes geometry, not a scene. Skinning streams are later.

module;
#include "Core/Prelude.h"

export module raptor.render:mesh_gpu;

import raptor.core;
import raptor.rhi;
import raptor.geometry;
import :resources;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// GPU location of one mesh, ready to bind + draw: the (pooled) vertex + index buffers and the
// byte offsets of this mesh's streams within them.
struct MeshGpu {
    rhi::Buffer*     vertexBuffer = nullptr;
    u64              vertexOffset = 0;
    rhi::Buffer*     indexBuffer  = nullptr;
    u64              indexOffset  = 0;
    u32              indexCount   = 0;
    rhi::IndexFormat indexFormat  = rhi::IndexFormat::UInt32;
};

class MeshGpuCache {
public:
    explicit MeshGpuCache(rhi::Device& device) noexcept
        : m_queue(device.GetQueue(rhi::QueueType::Graphics)),
          m_vertexPool(device, rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst, kVertexChunk, u8"mesh.vertexPool"),
          m_indexPool(device, rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst, kIndexChunk, u8"mesh.indexPool") {}

    ~MeshGpuCache() { Clear(); }

    MeshGpuCache(const MeshGpuCache&) = delete;
    MeshGpuCache& operator=(const MeshGpuCache&) = delete;

    // Uploads `mesh` on first request (sub-allocating from the pools), returns its cached GPU
    // location (null if empty or allocation failed). The pointer is stable until Clear().
    [[nodiscard]] const MeshGpu* GetOrUpload(geometry::StaticMesh* mesh) {
        if (mesh == nullptr || mesh->VertexCount() == 0 || mesh->IndexCount() == 0) { return nullptr; }
        if (MeshGpu* cached = m_cache.Find(mesh)) { return cached; }

        const GpuBufferPool::Alloc v = m_vertexPool.Allocate(mesh->VertexDataSize(), kVertexAlign);
        const GpuBufferPool::Alloc idx = m_indexPool.Allocate(mesh->indices.DataSize(), kIndexAlign);
        if (!v.ok || !idx.ok) { return nullptr; }

        MeshGpu g;
        g.vertexBuffer = v.buffer;   g.vertexOffset = v.offset;
        g.indexBuffer  = idx.buffer; g.indexOffset  = idx.offset;
        g.indexCount   = mesh->IndexCount();
        g.indexFormat  = (mesh->indices.GetFormat() == geometry::IndexBuffer::Format::U16)
                            ? rhi::IndexFormat::UInt16 : rhi::IndexFormat::UInt32;

        if (m_queue != nullptr) {
            rhi::TransferBatch* tb = nullptr;
            if (m_queue->CreateTransferBatch(tb).IsOk() && tb != nullptr) {
                tb->WriteBuffer(g.vertexBuffer, g.vertexOffset, Span<const u8>{ mesh->VertexData(), mesh->VertexDataSize() });
                tb->WriteBuffer(g.indexBuffer, g.indexOffset, Span<const u8>{ mesh->indices.RawData(), mesh->indices.DataSize() });
                (void)tb->Submit();
                m_queue->DestroyTransferBatch(tb);
            }
        }

        m_cache.InsertOrAssign(mesh, g);
        return m_cache.Find(mesh);
    }

    // Frees all pooled GPU memory + the cache.
    void Clear() {
        m_vertexPool.Clear();
        m_indexPool.Clear();
        m_cache.Clear();
    }

    [[nodiscard]] usize Size() const noexcept { return m_cache.Size(); }

private:
    static constexpr u64 kVertexChunk = 4ull * 1024 * 1024;   // 4 MB vertex chunks
    static constexpr u64 kIndexChunk  = 1ull * 1024 * 1024;   // 1 MB index chunks
    static constexpr u64 kVertexAlign = 16;                   // safe vertex-stream offset alignment
    static constexpr u64 kIndexAlign  = 4;                    // covers u16 + u32 index offsets

    rhi::Queue*   m_queue;
    GpuBufferPool m_vertexPool;
    GpuBufferPool m_indexPool;
    HashMap<geometry::StaticMesh*, MeshGpu> m_cache;
};

} // namespace raptor::render
