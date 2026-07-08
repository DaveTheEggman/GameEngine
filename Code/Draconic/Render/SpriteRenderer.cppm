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

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Instanced billboard. The quad corners (6 verts) come from SV_VertexID; per-sprite data arrives as
// four instance-stepped float4 attributes (TEXCOORD0..3). Camera basis is pulled from the view matrix
// (row-major, row-vector convention): world-space right = column 0, up = column 1.
inline constexpr const char8_t* kSpriteVS = u8R"(
#pragma pack_matrix(row_major)
cbuffer SpriteView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
};
struct VSIn {
    float4 PositionSize    : TEXCOORD0;   // xyz = world center, w = width
    float4 SizeOrientation : TEXCOORD1;   // x = height, y = orientation mode
    float4 Tint            : TEXCOORD2;
    float4 UVRect          : TEXCOORD3;    // xy = uv min, zw = uv size
    uint   VertexID        : SV_VertexID;
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
    float2 size     = float2(i.PositionSize.w, i.SizeOrientation.x);
    int    mode     = (int)(i.SizeOrientation.y + 0.5);

    // Billboard basis in world space.
    float3 right, up;
    if (mode == 2) {                     // world-aligned (XY plane)
        right = float3(1, 0, 0); up = float3(0, 1, 0);
    } else if (mode == 1) {              // camera-facing about world Y (right in XZ, up = world Y)
        float3 camRight = float3(View._m00, View._m10, View._m20);
        right = normalize(float3(camRight.x, 0, camRight.z));
        up    = float3(0, 1, 0);
    } else {                             // full camera-facing
        right = float3(View._m00, View._m10, View._m20);
        up    = float3(View._m01, View._m11, View._m21);
    }

    float2 local = CORNERS[i.VertexID];
    float3 cornerWS = worldPos + right * (local.x * size.x) + up * (local.y * size.y);
    o.pos = mul(float4(cornerWS, 1.0), ViewProj);
    o.uv  = i.UVRect.xy + UVS[i.VertexID] * i.UVRect.zw;
    o.col = i.Tint;
    return o;
}
)";

inline constexpr const char8_t* kSpritePS = u8R"(
Texture2D    SpriteTexture : register(t0, space1);
SamplerState SpriteSampler : register(s0, space1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : COLOR0) : SV_Target {
    return SpriteTexture.Sample(SpriteSampler, uv) * col;
}
)";

// One instance record uploaded to the instance stream (64 bytes = 4x float4, matches the VS inputs).
struct SpriteInstance {
    Float4 positionSize;      // xyz world center, w width
    Float4 sizeOrientation;   // x height, y orientation mode
    Float4 tint;
    Float4 uvRect;            // xy uv min, zw uv size
};
static_assert(sizeof(SpriteInstance) == 64);

class SpriteRenderer final : public Renderer {
public:
    SpriteRenderer(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders),
          m_instanceRing(device, framesInFlight, sizeof(SpriteInstance),
                         rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst, u8"sprite.instances"),
          m_viewRing(device, framesInFlight, kViewSlotSize,
                     rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"sprite.view") {}
    ~SpriteRenderer() override { Shutdown(); }
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"sprite", shaders::ShaderStage::Vertex,   kSpriteVS);
        m_shaders->RegisterSource(u8"sprite", shaders::ShaderStage::Fragment, kSpritePS);

        // set 0: view UBO (dynamic offset, VS). set 1: sprite texture + sampler (FS).
        rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        viewEntry.hasDynamicOffset = true;
        rhi::BindGroupLayoutDesc vld{}; vld.entries = Span<const rhi::BindGroupLayoutEntry>{ &viewEntry, 1 };
        if (!m_device->CreateBindGroupLayout(vld, m_viewLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayoutEntry texEntries[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc tld{}; tld.entries = Span<const rhi::BindGroupLayoutEntry>{ texEntries, 2 };
        if (!m_device->CreateBindGroupLayout(tld, m_texLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_viewLayout, m_texLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 2 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc sd{};
        sd.minFilter = rhi::FilterMode::Linear; sd.magFilter = rhi::FilterMode::Linear;
        sd.addressU = rhi::AddressMode::ClampToEdge; sd.addressV = rhi::AddressMode::ClampToEdge; sd.addressW = rhi::AddressMode::ClampToEdge;
        if (!m_device->CreateSampler(sd, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        // Static 6-index buffer [0..5] so the SV_VertexID quad renders through DrawIndexed.
        const u16 indices[6] = { 0, 1, 2, 3, 4, 5 };
        rhi::BufferDesc ibd{};
        ibd.size = sizeof(indices); ibd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
        ibd.memory = rhi::MemoryLocation::CpuToGpu; ibd.label = u8"sprite.indices";
        if (!m_device->CreateBuffer(ibd, m_indexBuffer).IsOk()) { return Status{ ErrorCode::Unknown }; }
        if (void* p = m_indexBuffer->Map()) { MemCopy(p, indices, sizeof(indices)); m_indexBuffer->Unmap(); }

        return Status{};
    }

    // ---- Renderer ----
    [[nodiscard]] Span<const RenderCategory> SupportedCategories() const override {
        static const RenderCategory cats[] = { RenderCategories::Transparent };
        return Span<const RenderCategory>{ cats, 1 };
    }

    void PrepareFrame(u32 maxDraws, u32 frameIndex) override {
        // One draw pass (blended forward), so the instance ring only needs `maxDraws` (no cascade/prepass
        // re-emit factor). The view ring needs a slot per view; kMaxViews is a safe upper bound.
        m_instanceRing.Reserve(maxDraws == 0 ? 1u : maxDraws);
        m_viewRing.Reserve(kMaxViews);
        m_instanceRing.BeginFrame(frameIndex);
        m_viewRing.BeginFrame(frameIndex);
    }

    void Resolve(const RenderRecordContext& ctx, Span<const DrawItem> items, Array<ResolvedDraw>& out) override {
        if (items.IsEmpty()) { return; }
        m_depthFormat = ctx.depthFormat;   // match the blended pass's depth attachment (for the PSO)
        rhi::BindGroup* viewBg = EnsureViewBindGroup();
        if (viewBg == nullptr) { return; }

        // This view's ViewProj + View into the view ring (dynamic-offset uniform).
        const DynamicUniformRing::Range vr = m_viewRing.Allocate();
        if (!vr.ok) { return; }
        struct ViewUBO { Float4x4 viewProj; Float4x4 view; } ubo{ ctx.viewProj, ctx.viewMatrix };
        MemCopy(vr.ptr, &ubo, sizeof(ubo));

        // Fuse consecutive sprites sharing (texture, blend) into one instanced draw.
        usize i = 0;
        while (i < items.Size()) {
            const auto* head = static_cast<const SpriteRenderData*>(items[i].data);
            usize j = i + 1;
            while (j < items.Size()) {
                const auto* nd = static_cast<const SpriteRenderData*>(items[j].data);
                if (nd->texture != head->texture || nd->additive != head->additive) { break; }
                ++j;
            }
            const u32 count = static_cast<u32>(j - i);
            const DynamicUniformRing::Range ir = m_instanceRing.AllocateRange(count);
            if (ir.ok) {
                auto* inst = static_cast<SpriteInstance*>(ir.ptr);
                for (usize k = i; k < j; ++k) {
                    const auto* s = static_cast<const SpriteRenderData*>(items[k].data);
                    SpriteInstance& si = inst[k - i];
                    si.positionSize    = Float4{ s->worldCenter.x, s->worldCenter.y, s->worldCenter.z, s->size.x };
                    si.sizeOrientation = Float4{ s->size.y, static_cast<f32>(s->orientation), 0.0f, 0.0f };
                    si.tint            = Float4{ s->tint.r, s->tint.g, s->tint.b, s->tint.a };
                    si.uvRect          = s->uvRect;
                }
                rhi::RenderPipeline* pso = EnsurePipeline(ctx.colorFormat, head->additive);
                rhi::BindGroup* texBg = EnsureTextureBindGroup(head->texture);
                if (pso != nullptr && texBg != nullptr) {
                    ResolvedDraw d{};
                    d.pso           = pso;
                    d.viewSet       = viewBg;  d.viewDynamic = true; d.viewOffset = vr.byteOffset;
                    d.drawSet       = texBg;   // set 1 = sprite texture (see shader register space1)
                    d.vertexBuffer0 = m_instanceRing.Buffer(); d.vertexOffset0 = ir.byteOffset;
                    d.indexBuffer   = m_indexBuffer; d.indexFormat = rhi::IndexFormat::UInt16; d.indexCount = 6;
                    d.instanceCount = count;
                    out.PushBack(d);
                }
            }
            i = j;
        }
    }

    void FinishFrame() override {
        m_instanceRing.EndFrame();
        m_viewRing.EndFrame();
    }

private:
    static constexpr u32 kMaxViews     = 8;
    static constexpr u64 kViewSlotSize = 256;   // 2x mat4 padded to the dynamic-uniform alignment

    // One bind group over the whole view ring (per-draw dynamic offset selects the slot). Rebuilt ONLY
    // when the ring reallocates (generation bump) - which drains the GPU first (Reserve's WaitIdle) - so
    // we never free a descriptor set an in-flight frame still references.
    rhi::BindGroup* EnsureViewBindGroup() {
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

    rhi::BindGroup* EnsureTextureBindGroup(rhi::TextureView* tex) {
        if (tex == nullptr) { return nullptr; }
        if (rhi::BindGroup** found = m_texBindGroups.Find(tex)) { return *found; }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(tex),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_texLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_texBindGroups.InsertOrAssign(tex, bg);
        return bg;
    }

    rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat colorFormat, bool additive) {
        Pipelines& p = additive ? m_additive : m_alpha;
        if (p.pso != nullptr && p.format == colorFormat) { return p.pso; }
        if (p.pso != nullptr) { m_device->DestroyRenderPipeline(p.pso); p.pso = nullptr; }

        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"sprite", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"sprite", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }

        const rhi::VertexAttribute attrs[] = {
            { rhi::VertexFormat::Float32x4,  0, 0 },
            { rhi::VertexFormat::Float32x4, 16, 1 },
            { rhi::VertexFormat::Float32x4, 32, 2 },
            { rhi::VertexFormat::Float32x4, 48, 3 },
        };
        rhi::VertexBufferLayout vbl{};
        vbl.stride = sizeof(SpriteInstance); vbl.stepMode = rhi::VertexStepMode::Instance;
        vbl.attributes = Span<const rhi::VertexAttribute>{ attrs, 4 };

        rhi::ColorTargetState target{};
        target.format = colorFormat;
        // Additive sprites use ALPHA-WEIGHTED additive (dst + src.rgb*src.a), not the plain {One,One}
        // accumulate preset - so a texture's transparent (alpha 0) texels add nothing instead of dumping
        // their (often white) RGB into the scene. Only the opaque logo glows.
        const rhi::BlendState addBlend{
            { rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One, rhi::BlendOperation::Add },
            { rhi::BlendFactor::One,      rhi::BlendFactor::One, rhi::BlendOperation::Add } };
        target.blend  = additive ? addBlend : rhi::BlendState::AlphaBlend();

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
        pd.label = additive ? u8"sprite.additive" : u8"sprite.alpha";
        rhi::RenderPipeline* pso = nullptr;
        if (!m_device->CreateRenderPipeline(pd, pso).IsOk()) { return nullptr; }
        p.pso = pso; p.format = colorFormat;
        return pso;
    }

    void Shutdown() {
        for (auto& kv : m_texBindGroups) { if (kv.value != nullptr) { m_device->DestroyBindGroup(kv.value); } }
        m_texBindGroups.Clear();
        if (m_viewBg != nullptr) { m_device->DestroyBindGroup(m_viewBg); m_viewBg = nullptr; }
        if (m_alpha.pso != nullptr) { m_device->DestroyRenderPipeline(m_alpha.pso); m_alpha.pso = nullptr; }
        if (m_additive.pso != nullptr) { m_device->DestroyRenderPipeline(m_additive.pso); m_additive.pso = nullptr; }
        if (m_indexBuffer != nullptr) { m_device->DestroyBuffer(m_indexBuffer); m_indexBuffer = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_texLayout != nullptr) { m_device->DestroyBindGroupLayout(m_texLayout); m_texLayout = nullptr; }
        if (m_viewLayout != nullptr) { m_device->DestroyBindGroupLayout(m_viewLayout); m_viewLayout = nullptr; }
    }

    struct Pipelines { rhi::RenderPipeline* pso = nullptr; rhi::TextureFormat format = rhi::TextureFormat::Undefined; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    DynamicUniformRing     m_instanceRing;
    DynamicUniformRing     m_viewRing;
    rhi::BindGroupLayout*  m_viewLayout = nullptr;
    rhi::BindGroupLayout*  m_texLayout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::Sampler*          m_sampler = nullptr;
    rhi::Buffer*           m_indexBuffer = nullptr;
    rhi::BindGroup*        m_viewBg = nullptr;
    u32                    m_viewBgGen = 0xFFFFFFFFu;   // generation the view bind group was built for
    Pipelines              m_alpha;
    Pipelines              m_additive;
    rhi::TextureFormat     m_depthFormat = rhi::TextureFormat::Depth32Float;
    HashMap<rhi::TextureView*, rhi::BindGroup*> m_texBindGroups;
};

} // namespace draconic::render
