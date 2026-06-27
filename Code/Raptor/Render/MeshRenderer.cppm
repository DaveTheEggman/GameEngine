/// Raptor::Render — the `:mesh_renderer` partition.
///
/// `MeshRenderer` is the `Renderer` for the mesh categories (Opaque/Masked/Transparent). It
/// owns the built-in forward shader (two permutations), the GPU rings, and the mesh cache,
/// and records draws for the mesh `DrawItem`s it is handed.
///
/// HYBRID instancing (§8): a run of consecutive draws sharing one mesh + material is issued as
/// a single INSTANCED draw; a lone draw takes the simpler per-object path. Both read the
/// view-projection from a shared per-view UBO (set 0). The per-object path adds a per-object
/// UBO (set 1, world + tint, dynamic offset); the instanced path adds a per-instance
/// `StructuredBuffer<InstanceData>` (set 1) indexed by a uint4 DataOffsets vertex stream
/// (location 5, instance-stepped) — the portable base+offset addressing (NOT SV_InstanceID,
/// which differs between DX12 and Vulkan). Transparent draws are never instanced (back-to-front
/// order must dominate). Material set-2 binding + real lighting are later phases.

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

// Built-in forward shader, two permutations selected by INSTANCED. The view-projection is in a
// per-view UBO (set 0); the world matrix + tint come either from a per-object UBO (set 1) or,
// in the instanced permutation, from a per-instance StructuredBuffer indexed by the uint4
// DataOffsets vertex attribute (location 5). Vertex inputs use the RHI's TEXCOORDn convention
// (semantic index = location), matching VertexLayoutType::Mesh at locations 0..4.
inline constexpr const char8_t* kForwardVS = u8R"(
cbuffer View : register(b0, space0) {
    row_major float4x4 ViewProj;   // Raptor matrices are row-major; annotate so HLSL reads them right.
};
#ifdef INSTANCED
struct InstanceData { row_major float4x4 World; float4 Tint; };
StructuredBuffer<InstanceData> Instances : register(t0, space1);
#else
cbuffer Object : register(b0, space1) {
    row_major float4x4 World;
    float4             Tint;
};
#endif
struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
#ifdef INSTANCED
    uint4  dataOffsets : TEXCOORD5;   // .x = index into Instances[] (instance-stepped)
#endif
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
#ifdef INSTANCED
    float4x4 world = Instances[input.dataOffsets.x].World;
    float4   tint  = Instances[input.dataOffsets.x].Tint;
#else
    float4x4 world = World;
    float4   tint  = Tint;
#endif
    float4 worldPos = mul(float4(input.position, 1.0), world);
    o.clip      = mul(worldPos, ViewProj);
    o.normalWS  = normalize(mul(float4(input.normal, 0.0), world).xyz);
    o.color     = input.color * tint;                           // vertex color * per-instance tint
    o.uv        = input.uv;                                     // consume the full vertex layout
    o.tangentWS = mul(float4(input.tangent, 0.0), world).xyz;
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
    return float4(ndl * input.color.rgb, 1.0);                  // lambert * (vertex color * tint)
}
)";

class MeshRenderer final : public Renderer {
public:
    MeshRenderer(rhi::Device& device, shaders::ShaderSystem& shaderSystem,
                 materials::PipelineStateCache& psoCache, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaderSystem), m_psoCache(&psoCache), m_meshes(device),
          m_viewRing(device, framesInFlight, kViewSlot, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"mesh.view"),
          m_objectRing(device, framesInFlight, kViewSlot, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"mesh.object"),
          m_instanceRing(device, framesInFlight, sizeof(InstanceData), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"mesh.instances"),
          m_offsetsRing(device, framesInFlight, sizeof(DataOffsets), rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst, u8"mesh.offsets") {}

    ~MeshRenderer() override { Shutdown(); }

    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    // Registers the forward shader + creates the bind-group / pipeline layouts.
    Status Initialize() {
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Vertex,   kForwardVS);
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Fragment, kForwardPS);

        // set 0: per-view UBO (ViewProj), dynamic offset.
        rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        viewEntry.hasDynamicOffset = true;
        if (!MakeLayout(viewEntry, m_viewLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 1 (non-instanced): per-object UBO (World + Tint), dynamic offset.
        rhi::BindGroupLayoutEntry objEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        objEntry.hasDynamicOffset = true;
        if (!MakeLayout(objEntry, m_objectLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 1 (instanced): per-instance StructuredBuffer (read-only storage).
        rhi::BindGroupLayoutEntry instEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Vertex, /*readOnly*/ true);
        if (!MakeLayout(instEntry, m_instanceLayout)) { return Status{ ErrorCode::Unknown }; }

        if (!MakePipelineLayout(m_viewLayout, m_objectLayout,   m_pipelineLayoutSingle))   { return Status{ ErrorCode::Unknown }; }
        if (!MakePipelineLayout(m_viewLayout, m_instanceLayout, m_pipelineLayoutInstanced)) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // ---- Renderer ----

    [[nodiscard]] Span<const RenderCategory> SupportedCategories() const override {
        static constexpr RenderCategory kCats[] = {
            RenderCategories::Opaque, RenderCategories::Masked, RenderCategories::Transparent };
        return Span<const RenderCategory>{ kCats, 3 };
    }

    // Size every ring for the whole frame's draws (once) + select this frame's region.
    void PrepareFrame(u32 maxDraws, u32 frameIndex) override {
        m_ready = false;
        if (maxDraws == 0) { return; }
        if (!m_viewRing.Reserve(maxDraws) || !m_objectRing.Reserve(maxDraws) ||
            !m_instanceRing.Reserve(maxDraws) || !m_offsetsRing.Reserve(maxDraws)) { return; }
        if (!EnsureBindGroup(m_viewRing,     m_viewLayout,     sizeof(ViewData),   m_viewBG,     m_viewBGGen,     /*whole*/ false) ||
            !EnsureBindGroup(m_objectRing,   m_objectLayout,   sizeof(ObjectData), m_objectBG,   m_objectBGGen,   /*whole*/ false) ||
            !EnsureBindGroup(m_instanceRing, m_instanceLayout, 0,                  m_instanceBG, m_instanceBGGen, /*whole*/ true)) { return; }
        m_viewRing.BeginFrame(frameIndex);
        m_objectRing.BeginFrame(frameIndex);
        m_instanceRing.BeginFrame(frameIndex);
        m_offsetsRing.BeginFrame(frameIndex);
        m_ready = true;
    }

    void Record(const RenderRecordContext& ctx, Span<const DrawItem> items) override {
        if (!m_ready || ctx.pass == nullptr || items.IsEmpty()) { return; }

        // Per-view UBO (shared by every draw in this call): write ViewProj into a view slot.
        // The two pipeline layouts share set 0 (m_viewLayout), so the binding persists across
        // pipeline switches; each draw (re)binds it after SetPipeline (sets follow the pipeline).
        const DynamicUniformRing::Range view = m_viewRing.Allocate();
        if (!view.ok) { return; }
        *static_cast<ViewData*>(view.ptr) = ViewData{ ctx.viewProj };
        const u32 viewOffset = view.byteOffset;

        const bool allowInstancing = (items[0].data->category != RenderCategories::Transparent);

        usize i = 0;
        while (i < items.Size()) {
            const auto* head = static_cast<const MeshRenderData*>(items[i].data);
            // Extend the run while mesh + material match (a batchable group).
            usize j = i + 1;
            if (allowInstancing) {
                while (j < items.Size()) {
                    const auto* nd = static_cast<const MeshRenderData*>(items[j].data);
                    if (nd->mesh != head->mesh || nd->material != head->material) { break; }
                    ++j;
                }
            }
            const u32 runLen = static_cast<u32>(j - i);

            const MeshGpu* mesh = m_meshes.GetOrUpload(head->mesh);
            if (mesh != nullptr) {
                if (runLen >= 2) { RecordInstanced(ctx, viewOffset, items, i, runLen, *head, *mesh); }
                else             { RecordSingle(ctx, viewOffset, *head, *mesh); }
            }
            i = j;
        }
    }

    void FinishFrame() override {
        m_viewRing.EndFrame();
        m_objectRing.EndFrame();
        m_instanceRing.EndFrame();
        m_offsetsRing.EndFrame();
        m_ready = false;
    }

private:
    struct ViewData     { Mat4 viewProj; };              // 64
    struct ObjectData   { Mat4 world; Color tint; };     // 80  (cbuffer Object: World + Tint)
    struct InstanceData { Mat4 world; Color tint; };     // 80  (StructuredBuffer element)
    struct DataOffsets  { u32 x, y, z, w; };             // 16  (instance-stepped vertex attr)

    static constexpr u64 kViewSlot = 256;                // dynamic UBO offset alignment

    void RecordSingle(const RenderRecordContext& ctx, u32 viewOffset, const MeshRenderData& md, const MeshGpu& mesh) {
        materials::PipelineConfig config = ConfigFor(md, ctx, /*instanced*/ false);
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayoutSingle, ctx.colorFormat);
        if (pso == nullptr) { return; }

        const DynamicUniformRing::Range obj = m_objectRing.Allocate();
        if (!obj.ok) { return; }
        *static_cast<ObjectData*>(obj.ptr) = ObjectData{ md.world, md.color };

        const u32 objOffset = obj.byteOffset;
        ctx.pass->SetPipeline(pso);
        ctx.pass->SetBindGroup(0, m_viewBG, Span<const u32>{ &viewOffset, 1 });
        ctx.pass->SetBindGroup(1, m_objectBG, Span<const u32>{ &objOffset, 1 });
        ctx.pass->SetVertexBuffer(0, mesh.vertexBuffer);
        ctx.pass->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat);
        ctx.pass->DrawIndexed(mesh.indexCount);
    }

    void RecordInstanced(const RenderRecordContext& ctx, u32 viewOffset, Span<const DrawItem> items, usize first, u32 count,
                         const MeshRenderData& head, const MeshGpu& mesh) {
        materials::PipelineConfig config = ConfigFor(head, ctx, /*instanced*/ true);
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayoutInstanced, ctx.colorFormat);
        if (pso == nullptr) { return; }

        const DynamicUniformRing::Range inst = m_instanceRing.AllocateRange(count);
        const DynamicUniformRing::Range offs = m_offsetsRing.AllocateRange(count);
        if (!inst.ok || !offs.ok) { return; }

        InstanceData* id = static_cast<InstanceData*>(inst.ptr);
        DataOffsets*  od = static_cast<DataOffsets*>(offs.ptr);
        for (u32 k = 0; k < count; ++k) {
            const auto* md = static_cast<const MeshRenderData*>(items[first + k].data);
            id[k] = InstanceData{ md->world, md->color };
            od[k] = DataOffsets{ inst.slotIndex + k, 0, 0, 0 };   // absolute index into Instances[]
        }

        ctx.pass->SetPipeline(pso);
        ctx.pass->SetBindGroup(0, m_viewBG, Span<const u32>{ &viewOffset, 1 });
        ctx.pass->SetBindGroup(1, m_instanceBG, Span<const u32>{});      // whole buffer, no dynamic offset
        ctx.pass->SetVertexBuffer(0, mesh.vertexBuffer);
        ctx.pass->SetVertexBuffer(1, m_offsetsRing.Buffer(), offs.byteOffset);
        ctx.pass->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat);
        ctx.pass->DrawIndexed(mesh.indexCount, count);
    }

    [[nodiscard]] static materials::PipelineConfig ConfigFor(const MeshRenderData& md, const RenderRecordContext& ctx, bool instanced) {
        materials::PipelineConfig config = (md.material != nullptr)
            ? md.material->pipeline
            : materials::PipelineConfig::ForOpaqueMesh(u8"forward");
        config.depthFormat = ctx.depthFormat;
        config.instanced   = instanced;
        if (instanced) { config.shaderFlags |= shaders::ShaderFlags::Instanced; }
        return config;
    }

    bool MakeLayout(const rhi::BindGroupLayoutEntry& entry, rhi::BindGroupLayout*& out) {
        rhi::BindGroupLayoutDesc ld{};
        ld.entries = Span<const rhi::BindGroupLayoutEntry>{ &entry, 1 };
        return m_device->CreateBindGroupLayout(ld, out).IsOk();
    }

    bool MakePipelineLayout(rhi::BindGroupLayout* set0, rhi::BindGroupLayout* set1, rhi::PipelineLayout*& out) {
        rhi::BindGroupLayout* layouts[] = { set0, set1 };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 2 };
        return m_device->CreatePipelineLayout(pld, out).IsOk();
    }

    // (Re)create a bind group over a ring's buffer when the ring (re)allocated. `whole` binds
    // the entire buffer (storage, indexed); otherwise a `bindSize` window (dynamic-offset UBO).
    bool EnsureBindGroup(DynamicUniformRing& ring, rhi::BindGroupLayout* layout, u64 bindSize,
                         rhi::BindGroup*& bg, u32& bgGen, bool whole) {
        if (bg != nullptr && bgGen == ring.Generation()) { return true; }
        if (bg) { m_device->DestroyBindGroup(bg); bg = nullptr; }
        rhi::Buffer* buffer = ring.Buffer();
        if (buffer == nullptr) { return false; }
        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(buffer, 0, whole ? ring.ByteCapacity() : bindSize);
        rhi::BindGroupDesc bgd{};
        bgd.layout = layout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { bg = nullptr; return false; }
        bgGen = ring.Generation();
        return true;
    }

    void Shutdown() {
        m_meshes.Clear();
        if (m_viewBG)     { m_device->DestroyBindGroup(m_viewBG); m_viewBG = nullptr; }
        if (m_objectBG)   { m_device->DestroyBindGroup(m_objectBG); m_objectBG = nullptr; }
        if (m_instanceBG) { m_device->DestroyBindGroup(m_instanceBG); m_instanceBG = nullptr; }
        if (m_pipelineLayoutSingle)    { m_device->DestroyPipelineLayout(m_pipelineLayoutSingle); m_pipelineLayoutSingle = nullptr; }
        if (m_pipelineLayoutInstanced) { m_device->DestroyPipelineLayout(m_pipelineLayoutInstanced); m_pipelineLayoutInstanced = nullptr; }
        if (m_viewLayout)     { m_device->DestroyBindGroupLayout(m_viewLayout); m_viewLayout = nullptr; }
        if (m_objectLayout)   { m_device->DestroyBindGroupLayout(m_objectLayout); m_objectLayout = nullptr; }
        if (m_instanceLayout) { m_device->DestroyBindGroupLayout(m_instanceLayout); m_instanceLayout = nullptr; }
        // rings free their buffers in their destructors (m_device still valid after this).
    }

    rhi::Device*                   m_device;
    shaders::ShaderSystem*         m_shaders;
    materials::PipelineStateCache* m_psoCache;
    MeshGpuCache                   m_meshes;

    rhi::BindGroupLayout* m_viewLayout     = nullptr;
    rhi::BindGroupLayout* m_objectLayout   = nullptr;
    rhi::BindGroupLayout* m_instanceLayout = nullptr;
    rhi::PipelineLayout*  m_pipelineLayoutSingle    = nullptr;
    rhi::PipelineLayout*  m_pipelineLayoutInstanced = nullptr;

    DynamicUniformRing m_viewRing;
    DynamicUniformRing m_objectRing;
    DynamicUniformRing m_instanceRing;
    DynamicUniformRing m_offsetsRing;

    rhi::BindGroup* m_viewBG     = nullptr;
    rhi::BindGroup* m_objectBG   = nullptr;
    rhi::BindGroup* m_instanceBG = nullptr;
    u32 m_viewBGGen = 0, m_objectBGGen = 0, m_instanceBGGen = 0;
    bool m_ready = false;
};

} // namespace raptor::render
