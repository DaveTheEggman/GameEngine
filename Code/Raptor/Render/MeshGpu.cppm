/// Raptor::Render — the `:mesh_gpu` partition.
///
/// MeshGpuCache: uploads a StaticMesh's vertex + index streams to GPU buffers on first
/// use and caches them by mesh pointer (so repeated draws of the same mesh reuse the
/// buffers). Part of the scene-agnostic renderer — it consumes geometry, not a scene.
/// The static vertex stream is uploaded as-is; skinning streams are a later concern.

module;
#include "Core/Prelude.h"

export module raptor.render:mesh_gpu;

import raptor.core;
import raptor.rhi;
import raptor.geometry;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// GPU buffers for one mesh, ready to bind + draw.
struct MeshGpu {
    rhi::Buffer*     vertexBuffer = nullptr;
    rhi::Buffer*     indexBuffer  = nullptr;
    u32              indexCount   = 0;
    rhi::IndexFormat indexFormat  = rhi::IndexFormat::UInt32;
};

class MeshGpuCache {
public:
    explicit MeshGpuCache(rhi::Device& device) noexcept
        : m_device(&device), m_queue(device.GetQueue(rhi::QueueType::Graphics)) {}

    ~MeshGpuCache() { Clear(); }

    MeshGpuCache(const MeshGpuCache&) = delete;
    MeshGpuCache& operator=(const MeshGpuCache&) = delete;

    // Uploads `mesh` on first request, returns its cached GPU buffers (null if empty or
    // a buffer could not be created). The pointer is stable until Clear().
    [[nodiscard]] const MeshGpu* GetOrUpload(geometry::StaticMesh* mesh) {
        if (mesh == nullptr || mesh->VertexCount() == 0 || mesh->IndexCount() == 0) { return nullptr; }
        if (MeshGpu* cached = m_cache.Find(mesh)) { return cached; }

        MeshGpu g;
        g.indexCount  = mesh->IndexCount();
        g.indexFormat = (mesh->indices.GetFormat() == geometry::IndexBuffer::Format::U16)
                            ? rhi::IndexFormat::UInt16 : rhi::IndexFormat::UInt32;

        rhi::BufferDesc vbd{};
        vbd.size = mesh->VertexDataSize();
        vbd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        vbd.label = u8"mesh.vertices";
        if (!m_device->CreateBuffer(vbd, g.vertexBuffer).IsOk()) { return nullptr; }

        rhi::BufferDesc ibd{};
        ibd.size = mesh->indices.DataSize();
        ibd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
        ibd.label = u8"mesh.indices";
        if (!m_device->CreateBuffer(ibd, g.indexBuffer).IsOk()) {
            m_device->DestroyBuffer(g.vertexBuffer);
            return nullptr;
        }

        if (m_queue != nullptr) {
            rhi::TransferBatch* tb = nullptr;
            if (m_queue->CreateTransferBatch(tb).IsOk() && tb != nullptr) {
                tb->WriteBuffer(g.vertexBuffer, 0, Span<const u8>{ mesh->VertexData(), mesh->VertexDataSize() });
                tb->WriteBuffer(g.indexBuffer, 0, Span<const u8>{ mesh->indices.RawData(), mesh->indices.DataSize() });
                (void)tb->Submit();
                m_queue->DestroyTransferBatch(tb);
            }
        }

        m_cache.InsertOrAssign(mesh, g);
        return m_cache.Find(mesh);
    }

    // Frees all cached GPU buffers.
    void Clear() {
        for (auto& e : m_cache) {
            if (e.value.vertexBuffer) { m_device->DestroyBuffer(e.value.vertexBuffer); }
            if (e.value.indexBuffer)  { m_device->DestroyBuffer(e.value.indexBuffer); }
        }
        m_cache.Clear();
    }

    [[nodiscard]] usize Size() const noexcept { return m_cache.Size(); }

private:
    rhi::Device* m_device;
    rhi::Queue*  m_queue;
    HashMap<geometry::StaticMesh*, MeshGpu> m_cache;
};

} // namespace raptor::render
