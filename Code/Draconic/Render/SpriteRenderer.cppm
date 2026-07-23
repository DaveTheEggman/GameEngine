/// Draconic::Render - the `:sprite_renderer` partition.
///
/// A Renderer that draws textured billboard quads (sprites). Ported from SedulousEngine's
/// SpriteRenderer/sprite.hlsl: a 6-vertex quad is generated in the vertex shader from SV_VertexID and
/// hardware-instanced, with per-sprite data (position/size/uv-rect/tint/orientation) fed as instance-
/// stepped vertex attributes. Sprites register for the Transparent category and interleave with
/// transparent meshes by depth (per-item renderer dispatch), sharing the blended forward pass:
/// alpha/additive blend, depth-test LessEqual against the scene depth, no depth write, cull none.
///
/// Batching: within a resolved run, consecutive sprites sharing (texture, blend mode) fuse into one
/// instanced draw. Non-indexed geometry isn't expressible through ResolvedDraw/EmitDraw (it always
/// DrawIndexed), so a static 6-index buffer [0..5] drives the SV_VertexID quad.

module;
#include "Core/Prelude.h"

export module draconic.render:sprite_renderer;

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.shaders.system;
import :data;
import :pipeline;
import :resources;
import :sprite_shaders;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render
{

    // One instance record uploaded to the instance stream (64 bytes = 4x float4, matches the VS inputs).
    struct SpriteInstance
    {
        Float4 positionSize;    // xyz world center, w width
        Float4 sizeOrientation; // x height, y orientation mode
        Float4 tint;
        Float4 uvRect;    // xy uv min, zw uv size
        Float4 axisRight; // xyz entity right (EntityOriented)
        Float4 axisUp;    // xyz entity up (EntityOriented)
    };
    static_assert(sizeof(SpriteInstance) == 96);

    class SpriteRenderer final : public Renderer
    {
    public:
        SpriteRenderer(rhi::Device& device, shaders::ShaderSystem& shaders,
                       u32 framesInFlight) noexcept
            : m_device(&device), m_shaders(&shaders),
              m_instanceRing(device, framesInFlight, sizeof(SpriteInstance),
                             rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst,
                             u8"sprite.instances"),
              m_viewRing(device, framesInFlight, kViewSlotSize,
                         rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"sprite.view")
        {
        }
        ~SpriteRenderer() override { Shutdown(); }
        SpriteRenderer(const SpriteRenderer&) = delete;
        SpriteRenderer& operator=(const SpriteRenderer&) = delete;

        Status Initialize();

        // ---- Renderer ----
        [[nodiscard]] Span<const RenderCategory> SupportedCategories() const override;

        void PrepareFrame(u32 maxDraws, u32 frameIndex) override;

        void Resolve(const RenderRecordContext& ctx, Span<const DrawItem> items,
                     Array<ResolvedDraw>& out) override;

        void FinishFrame() override;

    private:
        static constexpr u32 kMaxViews = 8;
        static constexpr u64 kViewSlotSize = 256; // 2x mat4 padded to the dynamic-uniform alignment

        // One bind group over the whole view ring (per-draw dynamic offset selects the slot). Rebuilt ONLY
        // when the ring reallocates (generation bump) - which drains the GPU first (Reserve's WaitIdle) - so
        // we never free a descriptor set an in-flight frame still references.
        rhi::BindGroup* EnsureViewBindGroup();

        rhi::BindGroup* EnsureTextureBindGroup(rhi::TextureView* tex);

        rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat colorFormat, bool additive);

        void Shutdown();

        struct Pipelines
        {
            rhi::RenderPipeline* pso = nullptr;
            rhi::TextureFormat format = rhi::TextureFormat::Undefined;
        };

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;
        DynamicUniformRing m_instanceRing;
        DynamicUniformRing m_viewRing;
        rhi::BindGroupLayout* m_viewLayout = nullptr;
        rhi::BindGroupLayout* m_texLayout = nullptr;
        rhi::PipelineLayout* m_pipelineLayout = nullptr;
        rhi::Sampler* m_sampler = nullptr;
        rhi::Buffer* m_indexBuffer = nullptr;
        rhi::BindGroup* m_viewBg = nullptr;
        u32 m_viewBgGen = 0xFFFFFFFFu; // generation the view bind group was built for
        Pipelines m_alpha;
        Pipelines m_additive;
        rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Depth32Float;
        HashMap<rhi::TextureView*, rhi::BindGroup*> m_texBindGroups;
    };

} // namespace draconic::render
