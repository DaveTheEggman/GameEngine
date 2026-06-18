// Raptor::RenderGraph — :transient_pool partition
//
// Pools GPU textures for reuse across frames by transient resources, avoiding
// per-frame allocation thrashing. Matches by exact descriptor; ages out unused
// entries. Ported from Sedulous.RenderGraph (TransientTexturePool.bf).

module;
#include "Core/Prelude.h"

export module raptor.rendergraph:transient_pool;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rendergraph
{
    namespace rhi = raptor::rhi;

    class TransientTexturePool
    {
    public:
        explicit TransientTexturePool(rhi::Device& device) noexcept : m_device(&device) {}
        ~TransientTexturePool() { DestroyAll(); }

        TransientTexturePool(const TransientTexturePool&) = delete;
        TransientTexturePool& operator=(const TransientTexturePool&) = delete;

        i32 maxUnusedFrames = 4;

        // Acquire a matching texture from the pool; true if one was found.
        [[nodiscard]] bool TryAcquire(const rhi::TextureDesc& desc, rhi::Texture*& texture, rhi::TextureView*& view)
        {
            for (usize i = 0; i < m_pool.Size(); ++i)
            {
                if (DescriptorsMatch(m_pool[i].desc, desc))
                {
                    texture = m_pool[i].texture;
                    view = m_pool[i].view;
                    m_pool.RemoveAt(i);
                    return true;
                }
            }
            texture = nullptr;
            view = nullptr;
            return false;
        }

        void ReturnToPool(const rhi::TextureDesc& desc, rhi::Texture* texture, rhi::TextureView* view)
        {
            m_pool.PushBack(PooledTexture{ desc, texture, view, 0 });
        }

        // Age out entries unused for more than maxUnusedFrames.
        void EndFrame()
        {
            for (usize i = m_pool.Size(); i-- > 0;)
            {
                ++m_pool[i].unusedFrames;
                if (m_pool[i].unusedFrames > maxUnusedFrames)
                {
                    rhi::Texture* tex = m_pool[i].texture;
                    rhi::TextureView* view = m_pool[i].view;
                    if (view != nullptr) { m_device->DestroyTextureView(view); }
                    if (tex != nullptr) { m_device->DestroyTexture(tex); }
                    m_pool.RemoveAt(i);
                }
            }
        }

        void DestroyAll()
        {
            for (PooledTexture& entry : m_pool)
            {
                if (entry.view != nullptr) { m_device->DestroyTextureView(entry.view); }
                if (entry.texture != nullptr) { m_device->DestroyTexture(entry.texture); }
            }
            m_pool.Clear();
        }

    private:
        struct PooledTexture
        {
            rhi::TextureDesc desc;
            rhi::Texture* texture = nullptr;
            rhi::TextureView* view = nullptr;
            i32 unusedFrames = 0;
        };

        static bool DescriptorsMatch(const rhi::TextureDesc& a, const rhi::TextureDesc& b) noexcept
        {
            return a.dimension == b.dimension
                && a.format == b.format
                && a.width == b.width
                && a.height == b.height
                && a.depth == b.depth
                && a.arrayLayerCount == b.arrayLayerCount
                && a.mipLevelCount == b.mipLevelCount
                && a.sampleCount == b.sampleCount
                && a.usage == b.usage;
        }

        rhi::Device* m_device;
        Array<PooledTexture> m_pool;
    };
}
