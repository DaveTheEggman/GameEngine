/// Raptor::Render — the `:mesh_renderer` partition.
///
/// `MeshRenderer` is the `Renderer` for the mesh categories (Opaque/Masked/Transparent):
/// it owns the built-in forward shader, the per-object UBO, and the mesh GPU cache, and
/// records DrawIndexed calls for the mesh `DrawItem`s it is handed. It is the per-category
/// drawer that replaces the slice's monolithic ForwardRenderer — the draw mechanics are the
/// same (mesh upload, per-object world+view-proj UBO with dynamic offsets, PSO from the
/// material via the cache), but now plugged in behind the `Renderer` seam so other
/// renderable types extend the renderer identically, from outside. Material set-2 binding +
/// real lighting are later phases (a built-in N·L shade for now). (§6/§8.)

module;
#include "Core/Prelude.h"

export module raptor.render:mesh_renderer;

import raptor.core;
import raptor.rhi;
import raptor.geometry;
import raptor.shaders;
import raptor.shaders.system;
import raptor.materials;
import raptor.materials.pso;
import :data;
import :views;
import :pipeline;
import :resources;
import :mesh_gpu;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// Built-in forward shader: object-space transform + a simple directional N·L shade.
// Vertex inputs use the RHI's TEXCOORDn convention (semantic index = vertex location),
// matching VertexLayoutType::Mesh: position/normal/uv/color/tangent at locations 0..4.
inline constexpr const char8_t* kForwardVS = u8R"(
cbuffer Object : register(b0, space0) {
    row_major float4x4 World;      // Raptor matrices are row-major; HLSL defaults to
    row_major float4x4 ViewProj;   // column-major packing, so annotate to read them right.
};
struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
};
struct VSOutput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
};
VSOutput main(VSInput input) {
    VSOutput o;
    float4 worldPos = mul(float4(input.position, 1.0), World);
    o.clip      = mul(worldPos, ViewProj);
    o.normalWS  = normalize(mul(float4(input.normal, 0.0), World).xyz);
    o.color     = input.color;                                  // carry all vertex inputs through
    o.uv        = input.uv;                                     // so the full vertex layout is consumed
    o.tangentWS = mul(float4(input.tangent, 0.0), World).xyz;
    return o;
}
)";

inline constexpr const char8_t* kForwardPS = u8R"(
struct PSInput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
};
float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 L = normalize(float3(0.4, 0.8, 0.5));
    float  ndl = saturate(dot(N, L)) * 0.8 + 0.2;
    return float4(ndl * input.color.rgb, 1.0);                  // lambert * vertex color
}
)";

class MeshRenderer final : public Renderer {
public:
    MeshRenderer(rhi::Device& device, shaders::ShaderSystem& shaderSystem,
                 materials::PipelineStateCache& psoCache, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaderSystem), m_psoCache(&psoCache), m_meshes(device),
          m_objectRing(device, framesInFlight, kSlotSize) {}

    ~MeshRenderer() override { Shutdown(); }

    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    // Registers the forward shader + creates the object bind-group / pipeline layout.
    Status Initialize() {
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Vertex,   kForwardVS);
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Fragment, kForwardPS);

        rhi::BindGroupLayoutEntry entry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        entry.hasDynamicOffset = true;
        rhi::BindGroupLayoutDesc ld{};
        ld.entries = Span<const rhi::BindGroupLayoutEntry>{ &entry, 1 };
        if (!m_device->CreateBindGroupLayout(ld, m_objectLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_objectLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // ---- Renderer ----

    [[nodiscard]] Span<const RenderCategory> SupportedCategories() const override {
        static constexpr RenderCategory kCats[] = {
            RenderCategories::Opaque, RenderCategories::Masked, RenderCategories::Transparent };
        return Span<const RenderCategory>{ kCats, 3 };
    }

    // Size the object ring for the whole frame's draws (once) and select this frame's region.
    void PrepareFrame(u32 maxDraws, u32 frameIndex) override {
        if (maxDraws == 0) { return; }
        if (!m_objectRing.Reserve(maxDraws) || !EnsureBindGroup()) { return; }
        m_objectRing.BeginFrame(frameIndex);
    }

    void Record(const RenderRecordContext& ctx, Span<const DrawItem> items) override {
        if (ctx.pass == nullptr || m_objectBindGroup == nullptr) { return; }

        for (usize n = 0; n < items.Size(); ++n) {
            const auto* md = static_cast<const MeshRenderData*>(items[n].data);
            const MeshGpu* mesh = m_meshes.GetOrUpload(md->mesh);
            if (mesh == nullptr) { continue; }

            materials::PipelineConfig config = (md->material != nullptr)
                ? md->material->pipeline
                : materials::PipelineConfig::ForOpaqueMesh(u8"forward");
            config.depthFormat = ctx.depthFormat;

            rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayout, ctx.colorFormat);
            if (pso == nullptr) { continue; }

            // per-object data: world + the view's view-projection (row-vector: clip = p*W*VP),
            // into this frame's ring region (frames-in-flight safe, dynamic-offset bound).
            const DynamicUniformRing::Slot slot = m_objectRing.Allocate();
            if (!slot.ok) { continue; }   // region exhausted (Reserve sized it for the frame)
            ObjectData od{ md->world, ctx.viewProj };
            MemCopy(slot.ptr, &od, sizeof(od));

            const u32 dynamicOffset = slot.dynamicOffset;
            ctx.pass->SetPipeline(pso);
            ctx.pass->SetBindGroup(0, m_objectBindGroup, Span<const u32>{ &dynamicOffset, 1 });
            ctx.pass->SetVertexBuffer(0, mesh->vertexBuffer);
            ctx.pass->SetIndexBuffer(mesh->indexBuffer, mesh->indexFormat);
            ctx.pass->DrawIndexed(mesh->indexCount);
        }
    }

    void FinishFrame() override { m_objectRing.EndFrame(); }

private:
    struct ObjectData { Mat4 world; Mat4 viewProj; };          // 128 bytes
    static constexpr u64 kSlotSize = 256;                      // dynamic UBO offset alignment

    // (Re)create the object bind group over the ring's buffer when the ring (re)allocated.
    bool EnsureBindGroup() {
        if (m_objectBindGroup != nullptr && m_bgGeneration == m_objectRing.Generation()) { return true; }
        if (m_objectBindGroup) { m_device->DestroyBindGroup(m_objectBindGroup); m_objectBindGroup = nullptr; }

        rhi::Buffer* buffer = m_objectRing.Buffer();
        if (buffer == nullptr) { return false; }
        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(buffer, 0, sizeof(ObjectData));
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_objectLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, m_objectBindGroup).IsOk()) { m_objectBindGroup = nullptr; return false; }

        m_bgGeneration = m_objectRing.Generation();
        return true;
    }

    void Shutdown() {
        m_meshes.Clear();
        if (m_objectBindGroup) { m_device->DestroyBindGroup(m_objectBindGroup); m_objectBindGroup = nullptr; }
        if (m_pipelineLayout)  { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_objectLayout)    { m_device->DestroyBindGroupLayout(m_objectLayout); m_objectLayout = nullptr; }
        // m_objectRing frees its buffer in its destructor (after this, m_device still valid).
    }

    rhi::Device*                   m_device;
    shaders::ShaderSystem*         m_shaders;
    materials::PipelineStateCache* m_psoCache;
    MeshGpuCache                   m_meshes;

    rhi::BindGroupLayout* m_objectLayout    = nullptr;
    rhi::PipelineLayout*  m_pipelineLayout  = nullptr;
    rhi::BindGroup*       m_objectBindGroup = nullptr;
    DynamicUniformRing    m_objectRing;
    u32                   m_bgGeneration    = 0;   // ring generation the bind group was built for
};

} // namespace raptor::render
