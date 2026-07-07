// draconic.particles.subsystem:renderer - the dedicated billboard particle Renderer.
//
// Modeled on SpriteRenderer (SV_VertexID quad + hardware instancing + per-frame ring + blended
// forward pass), but with a richer per-instance record and VS: per-particle rotation and a
// velocity-stretched billboard mode. Each DrawItem is a ParticleBillboardRenderData BATCH (N
// instances), so a whole system draws as one instanced call - the particle count never reaches the
// draw-list sort. Registered with RenderSubsystem via the RegisterRenderer seam (rides Transparent).

module;
#include "Core/Prelude.h"

export module draconic.particles.subsystem:renderer;

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.shaders.system;
import draconic.render;   // Renderer, RenderRecordContext, ResolvedDraw, DrawItem, DynamicUniformRing, categories
import :renderdata;

using namespace draconic::core;
namespace rhi = draconic::rhi;
namespace shaders = draconic::shaders;
namespace render = draconic::render;

namespace draconic::particles
{
    // Quad from SV_VertexID + per-instance billboard. Rotation (mode 0) and velocity stretch are the
    // additions over the sprite VS. Camera basis from the View matrix (row-major, row-vector).
    inline constexpr const char8_t* kParticleVS = u8R"(
#pragma pack_matrix(row_major)
cbuffer ParticleView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
};
struct VSIn {
    float4 PositionSize : TEXCOORD0;   // xyz world center, w width
    float4 SizeRotMode  : TEXCOORD1;   // x height, y rotation(rad), z orientation mode
    float4 Color        : TEXCOORD2;
    float4 UVRect       : TEXCOORD3;   // xy uv min, zw uv size
    float4 Velocity     : TEXCOORD4;   // xyz world velocity, w stretch scale
    uint   VertexID     : SV_VertexID;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

static const float2 CORNERS[6] = {
    float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5),
    float2(-0.5,  0.5), float2(0.5, -0.5), float2( 0.5, 0.5)
};
static const float2 UVS[6] = {
    float2(0, 1), float2(1, 1), float2(0, 0),
    float2(0, 0), float2(1, 1), float2(1, 0)
};

VSOut main(VSIn i) {
    VSOut o;
    float3 worldPos = i.PositionSize.xyz;
    float2 size     = float2(i.PositionSize.w, i.SizeRotMode.x);
    int    mode     = (int)(i.SizeRotMode.z + 0.5);
    float  rot      = i.SizeRotMode.y;

    float3 camRight = float3(View._m00, View._m10, View._m20);
    float3 camUp    = float3(View._m01, View._m11, View._m21);
    float3 camFwd   = float3(View._m02, View._m12, View._m22);

    float3 right, up;
    if (i.Velocity.w > 0.0 && dot(i.Velocity.xyz, i.Velocity.xyz) > 1e-8) {
        // Stretched billboard: length axis follows velocity, width axis perpendicular in view.
        float3 v = i.Velocity.xyz;
        float speed = length(v);
        up = v / speed;
        right = normalize(cross(up, camFwd));
        size.y *= (1.0 + speed * i.Velocity.w);
    } else if (mode == 2) {              // world-aligned (XY plane)
        right = float3(1, 0, 0); up = float3(0, 1, 0);
    } else if (mode == 1) {              // camera-facing about world Y
        right = normalize(float3(camRight.x, 0, camRight.z));
        up    = float3(0, 1, 0);
    } else {                             // full camera-facing, with per-particle roll
        float s = sin(rot), c = cos(rot);
        right = camRight * c + camUp * s;
        up    = camUp * c - camRight * s;
    }

    float2 local = CORNERS[i.VertexID];
    float3 cornerWS = worldPos + right * (local.x * size.x) + up * (local.y * size.y);
    o.pos = mul(float4(cornerWS, 1.0), ViewProj);
    o.uv  = i.UVRect.xy + UVS[i.VertexID] * i.UVRect.zw;
    o.col = i.Color;
    return o;
}
)";

    inline constexpr const char8_t* kParticlePS = u8R"(
Texture2D    ParticleTexture : register(t0, space1);
SamplerState ParticleSampler : register(s0, space1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : COLOR0) : SV_Target {
    return ParticleTexture.Sample(ParticleSampler, uv) * col;
}
)";
}

export namespace draconic::particles
{
    class ParticleRenderer final : public render::Renderer
    {
    public:
        ParticleRenderer(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
            : m_device(&device), m_shaders(&shaders),
              m_instanceRing(device, framesInFlight, sizeof(ParticleBillboardInstance),
                             rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst, u8"particle.instances"),
              m_viewRing(device, framesInFlight, kViewSlotSize,
                         rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"particle.view") {}
        ~ParticleRenderer() override { Shutdown(); }
        ParticleRenderer(const ParticleRenderer&) = delete;
        ParticleRenderer& operator=(const ParticleRenderer&) = delete;

        core::Status Initialize()
        {
            m_shaders->RegisterSource(u8"particle", shaders::ShaderStage::Vertex,   kParticleVS);
            m_shaders->RegisterSource(u8"particle", shaders::ShaderStage::Fragment, kParticlePS);

            rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
            viewEntry.hasDynamicOffset = true;
            rhi::BindGroupLayoutDesc vld{}; vld.entries = Span<const rhi::BindGroupLayoutEntry>{ &viewEntry, 1 };
            if (!m_device->CreateBindGroupLayout(vld, m_viewLayout).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }

            rhi::BindGroupLayoutEntry texEntries[] = {
                rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
            };
            rhi::BindGroupLayoutDesc tld{}; tld.entries = Span<const rhi::BindGroupLayoutEntry>{ texEntries, 2 };
            if (!m_device->CreateBindGroupLayout(tld, m_texLayout).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }

            rhi::BindGroupLayout* layouts[] = { m_viewLayout, m_texLayout };
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 2 };
            if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }

            rhi::SamplerDesc sd{};
            sd.minFilter = rhi::FilterMode::Linear; sd.magFilter = rhi::FilterMode::Linear;
            sd.addressU = rhi::AddressMode::ClampToEdge; sd.addressV = rhi::AddressMode::ClampToEdge; sd.addressW = rhi::AddressMode::ClampToEdge;
            if (!m_device->CreateSampler(sd, m_sampler).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }

            const u16 indices[6] = { 0, 1, 2, 3, 4, 5 };
            rhi::BufferDesc ibd{};
            ibd.size = sizeof(indices); ibd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
            ibd.memory = rhi::MemoryLocation::CpuToGpu; ibd.label = u8"particle.indices";
            if (!m_device->CreateBuffer(ibd, m_indexBuffer).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }
            if (void* p = m_indexBuffer->Map()) { MemCopy(p, indices, sizeof(indices)); m_indexBuffer->Unmap(); }

            // 1x1 white default texture: untextured particles (component texture == null) draw as solid
            // color quads instead of nothing. Uploaded once via a transfer batch.
            rhi::TextureDesc wtd{};
            wtd.format = rhi::TextureFormat::RGBA8Unorm; wtd.width = 1; wtd.height = 1;
            wtd.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; wtd.label = u8"particle.white";
            if (!m_device->CreateTexture(wtd, m_whiteTex).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }
            rhi::TextureViewDesc wvd{}; wvd.format = rhi::TextureFormat::RGBA8Unorm; wvd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(m_whiteTex, wvd, m_whiteView).IsOk()) { return core::Status{ core::ErrorCode::Unknown }; }
            const u8 white[4] = { 255, 255, 255, 255 };
            if (rhi::Queue* q = m_device->GetQueue(rhi::QueueType::Graphics))
            {
                rhi::TransferBatch* tb = nullptr;
                if (q->CreateTransferBatch(tb).IsOk() && tb != nullptr)
                {
                    rhi::TextureDataLayout layout{}; layout.bytesPerRow = 4; layout.rowsPerImage = 1;
                    tb->WriteTexture(m_whiteTex, Span<const u8>{ white, 4 }, layout, rhi::Extent3D{ 1, 1, 1 });
                    (void)tb->Submit();
                    q->DestroyTransferBatch(tb);
                }
            }
            return core::Status{};
        }

        [[nodiscard]] Span<const render::RenderCategory> SupportedCategories() const override
        {
            static const render::RenderCategory cats[] = { render::RenderCategories::Transparent };
            return Span<const render::RenderCategory>{ cats, 1 };
        }

        void PrepareFrame(u32 maxDraws, u32 frameIndex) override
        {
            // maxDraws is the frame's draw-ITEM count, but a particle batch is one item holding many
            // INSTANCES. Size the instance ring by the largest instance total seen (rounded up in 8k
            // chunks so we don't reallocate every frame while a system fills). Ramps by one frame during
            // rapid growth, then holds. The ring never grows mid-frame (AllocateRange would fail).
            const u32 chunk = 8192u;
            const u32 want = Max(maxDraws, ((m_maxInstancesSeen + chunk - 1u) / chunk) * chunk);
            m_instanceRing.Reserve(want == 0 ? 1u : want);
            m_viewRing.Reserve(kMaxViews);
            m_instanceRing.BeginFrame(frameIndex);
            m_viewRing.BeginFrame(frameIndex);
        }

        void Resolve(const render::RenderRecordContext& ctx, Span<const render::DrawItem> items, Array<render::ResolvedDraw>& out) override
        {
            if (items.IsEmpty()) { return; }
            m_depthFormat = ctx.depthFormat;
            rhi::BindGroup* viewBg = EnsureViewBindGroup();
            if (viewBg == nullptr) { return; }

            const render::DynamicUniformRing::Range vr = m_viewRing.Allocate();
            if (!vr.ok) { return; }
            struct ViewUBO { Matrix4 viewProj; Matrix4 view; } ubo{ ctx.viewProj, ctx.viewMatrix };
            MemCopy(vr.ptr, &ubo, sizeof(ubo));

            // Fuse consecutive batches sharing (texture, blend) into one instanced draw.
            usize i = 0;
            while (i < items.Size())
            {
                const auto* head = static_cast<const ParticleBillboardRenderData*>(items[i].data);
                usize j = i + 1;
                u32 total = head->count;
                while (j < items.Size())
                {
                    const auto* nd = static_cast<const ParticleBillboardRenderData*>(items[j].data);
                    if (nd->texture != head->texture || nd->blend != head->blend) { break; }
                    total += nd->count; ++j;
                }
                if (total > 0)
                {
                    m_maxInstancesSeen = Max(m_maxInstancesSeen, total);   // track even if this frame's alloc fails (next frame reserves more)
                    const render::DynamicUniformRing::Range ir = m_instanceRing.AllocateRange(total);
                    if (ir.ok)
                    {
                        auto* dst = static_cast<ParticleBillboardInstance*>(ir.ptr);
                        u32 off = 0;
                        for (usize k = i; k < j; ++k)
                        {
                            const auto* b = static_cast<const ParticleBillboardRenderData*>(items[k].data);
                            if (b->count > 0 && b->instances != nullptr)
                            {
                                MemCopy(dst + off, b->instances, static_cast<usize>(b->count) * sizeof(ParticleBillboardInstance));
                                off += b->count;
                            }
                        }
                        rhi::RenderPipeline* pso = EnsurePipeline(ctx.colorFormat, head->blend != 0);
                        rhi::BindGroup* texBg = EnsureTextureBindGroup(head->texture != nullptr ? head->texture : m_whiteView);
                        if (pso != nullptr && texBg != nullptr && off > 0)
                        {
                            render::ResolvedDraw d{};
                            d.pso           = pso;
                            d.viewSet       = viewBg; d.viewDynamic = true; d.viewOffset = vr.byteOffset;
                            d.drawSet       = texBg;
                            d.vertexBuffer0 = m_instanceRing.Buffer(); d.vertexOffset0 = ir.byteOffset;
                            d.indexBuffer   = m_indexBuffer; d.indexFormat = rhi::IndexFormat::UInt16; d.indexCount = 6;
                            d.instanceCount = off;
                            out.PushBack(d);
                        }
                    }
                }
                i = j;
            }
        }

        void FinishFrame() override { m_instanceRing.EndFrame(); m_viewRing.EndFrame(); }

    private:
        static constexpr u32 kMaxViews     = 8;
        static constexpr u64 kViewSlotSize = 256;

        rhi::BindGroup* EnsureViewBindGroup()
        {
            const u32 gen = m_viewRing.Generation();
            if (m_viewBg != nullptr && m_viewBgGen == gen) { return m_viewBg; }
            if (m_viewBg != nullptr) { m_device->DestroyBindGroup(m_viewBg); m_viewBg = nullptr; }
            if (m_viewRing.Buffer() == nullptr) { return nullptr; }
            rhi::BindGroupEntry e = rhi::BindGroupEntry::BufferEntry(m_viewRing.Buffer(), 0, kViewSlotSize);
            rhi::BindGroupDesc bgd{}; bgd.layout = m_viewLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ &e, 1 };
            if (!m_device->CreateBindGroup(bgd, m_viewBg).IsOk()) { m_viewBg = nullptr; return nullptr; }
            m_viewBgGen = gen;
            return m_viewBg;
        }

        rhi::BindGroup* EnsureTextureBindGroup(rhi::TextureView* tex)
        {
            if (tex == nullptr) { return nullptr; }
            if (rhi::BindGroup** found = m_texBindGroups.Find(tex)) { return *found; }
            rhi::BindGroupEntry ent[] = { rhi::BindGroupEntry::TextureEntry(tex), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
            rhi::BindGroupDesc bgd{}; bgd.layout = m_texLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
            rhi::BindGroup* bg = nullptr;
            if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
            m_texBindGroups.InsertOrAssign(tex, bg);
            return bg;
        }

        rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat colorFormat, bool additive)
        {
            Pipelines& p = additive ? m_additive : m_alpha;
            if (p.pso != nullptr && p.format == colorFormat) { return p.pso; }
            if (p.pso != nullptr) { m_device->DestroyRenderPipeline(p.pso); p.pso = nullptr; }

            rhi::ShaderModule* vs = m_shaders->GetVariant(u8"particle", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
            rhi::ShaderModule* ps = m_shaders->GetVariant(u8"particle", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
            if (vs == nullptr || ps == nullptr) { return nullptr; }

            const rhi::VertexAttribute attrs[] = {
                { rhi::VertexFormat::Float32x4,  0, 0 },
                { rhi::VertexFormat::Float32x4, 16, 1 },
                { rhi::VertexFormat::Float32x4, 32, 2 },
                { rhi::VertexFormat::Float32x4, 48, 3 },
                { rhi::VertexFormat::Float32x4, 64, 4 },
            };
            rhi::VertexBufferLayout vbl{};
            vbl.stride = sizeof(ParticleBillboardInstance); vbl.stepMode = rhi::VertexStepMode::Instance;
            vbl.attributes = Span<const rhi::VertexAttribute>{ attrs, 5 };

            rhi::ColorTargetState target{};
            target.format = colorFormat;
            const rhi::BlendState addBlend{
                { rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One, rhi::BlendOperation::Add },
                { rhi::BlendFactor::One,      rhi::BlendFactor::One, rhi::BlendOperation::Add } };
            target.blend = additive ? addBlend : rhi::BlendState::AlphaBlend();

            rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
            frag.targets = Span<const rhi::ColorTargetState>{ &target, 1 };

            rhi::DepthStencilState ds{};
            ds.format = m_depthFormat; ds.depthTestEnabled = true; ds.depthWriteEnabled = false;
            ds.depthCompare = rhi::CompareFunction::LessEqual;

            rhi::RenderPipelineDesc pd{};
            pd.layout = m_pipelineLayout;
            pd.vertex.shader  = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
            pd.vertex.buffers = Span<const rhi::VertexBufferLayout>{ &vbl, 1 };
            pd.fragment = frag;
            pd.depthStencil = ds;
            pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            pd.primitive.cullMode = rhi::CullMode::None;
            pd.label = additive ? u8"particle.additive" : u8"particle.alpha";
            rhi::RenderPipeline* pso = nullptr;
            if (!m_device->CreateRenderPipeline(pd, pso).IsOk()) { return nullptr; }
            p.pso = pso; p.format = colorFormat;
            return pso;
        }

        void Shutdown()
        {
            for (auto& kv : m_texBindGroups) { if (kv.value != nullptr) { m_device->DestroyBindGroup(kv.value); } }
            m_texBindGroups.Clear();
            if (m_viewBg != nullptr) { m_device->DestroyBindGroup(m_viewBg); m_viewBg = nullptr; }
            if (m_alpha.pso != nullptr) { m_device->DestroyRenderPipeline(m_alpha.pso); m_alpha.pso = nullptr; }
            if (m_additive.pso != nullptr) { m_device->DestroyRenderPipeline(m_additive.pso); m_additive.pso = nullptr; }
            if (m_indexBuffer != nullptr) { m_device->DestroyBuffer(m_indexBuffer); m_indexBuffer = nullptr; }
            if (m_whiteView != nullptr) { m_device->DestroyTextureView(m_whiteView); m_whiteView = nullptr; }
            if (m_whiteTex != nullptr) { m_device->DestroyTexture(m_whiteTex); m_whiteTex = nullptr; }
            if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
            if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
            if (m_texLayout != nullptr) { m_device->DestroyBindGroupLayout(m_texLayout); m_texLayout = nullptr; }
            if (m_viewLayout != nullptr) { m_device->DestroyBindGroupLayout(m_viewLayout); m_viewLayout = nullptr; }
        }

        struct Pipelines { rhi::RenderPipeline* pso = nullptr; rhi::TextureFormat format = rhi::TextureFormat::Undefined; };

        rhi::Device*                m_device;
        shaders::ShaderSystem*      m_shaders;
        render::DynamicUniformRing  m_instanceRing;
        render::DynamicUniformRing  m_viewRing;
        rhi::BindGroupLayout*       m_viewLayout = nullptr;
        rhi::BindGroupLayout*       m_texLayout = nullptr;
        rhi::PipelineLayout*        m_pipelineLayout = nullptr;
        rhi::Sampler*               m_sampler = nullptr;
        rhi::Buffer*                m_indexBuffer = nullptr;
        rhi::Texture*               m_whiteTex = nullptr;    // 1x1 white default (untextured particles)
        rhi::TextureView*           m_whiteView = nullptr;
        rhi::BindGroup*             m_viewBg = nullptr;
        u32                         m_viewBgGen = 0;
        HashMap<rhi::TextureView*, rhi::BindGroup*> m_texBindGroups;
        Pipelines                   m_alpha;
        Pipelines                   m_additive;
        rhi::TextureFormat          m_depthFormat = rhi::TextureFormat::Undefined;
        u32                         m_maxInstancesSeen = 0;   // sizes the instance ring (see PrepareFrame)
    };
}
