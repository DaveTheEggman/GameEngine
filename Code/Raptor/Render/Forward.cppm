/// Raptor::Render — the `:forward` partition.
///
/// ForwardRenderer: the minimal single-pass forward path. Given an ExtractedView and a
/// color target, it draws each renderable — uploads the mesh (MeshGpuCache), pulls a PSO
/// from the PipelineStateCache (built from the material's pipeline config + the forward
/// shader), writes per-object data (world + view-proj) into a dynamic-offset uniform
/// buffer, and records a DrawIndexed. It owns its depth buffer + does its own render
/// pass. Scene-agnostic: it consumes render data, never a scene. Material set-2 binding
/// and lighting are deliberately minimal for now (a built-in N·L shade) — the data-driven
/// material bind groups slot in next.

module;
#include "Core/Prelude.h"

export module raptor.render:forward;

import raptor.core;
import raptor.rhi;
import raptor.geometry;
import raptor.shaders;
import raptor.shaders.system;
import raptor.materials;
import raptor.materials.pso;
import :data;
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

class ForwardRenderer {
public:
    ForwardRenderer(rhi::Device& device, shaders::ShaderSystem& shaderSystem,
                    materials::PipelineStateCache& psoCache) noexcept
        : m_device(&device), m_shaders(&shaderSystem), m_psoCache(&psoCache), m_meshes(device) {}

    ~ForwardRenderer() { Shutdown(); }

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

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

    // Draws `view` into `colorTarget` (a `width`x`height` render target of `colorFormat`),
    // clearing to `bg` and depth-testing against an owned depth buffer.
    void Render(const ExtractedView& view, rhi::CommandEncoder& encoder, rhi::TextureView* colorTarget,
                rhi::TextureFormat colorFormat, u32 width, u32 height, rhi::ClearColor bg) {
        if (colorTarget == nullptr || !EnsureDepth(width, height)) { return; }

        const u32 count = static_cast<u32>(view.renderables.Size());
        if (count > 0 && !EnsureObjectBuffer(count)) { return; }

        // Per-object data: world + the shared view-projection (row-vector: clip = pos*W*VP).
        const Mat4 viewProj = view.view * view.projection;
        if (count > 0) {
            if (void* mapped = m_objectBuffer->Map()) {
                u8* base = static_cast<u8*>(mapped);
                for (u32 i = 0; i < count; ++i) {
                    ObjectData od{ view.renderables[i].worldMatrix, viewProj };
                    MemCopy(base + static_cast<usize>(i) * kSlotSize, &od, sizeof(od));
                }
                m_objectBuffer->Unmap();
            }
        }

        rhi::RenderPassDesc rp{};
        rhi::ColorAttachment ca{};
        ca.view = colorTarget; ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store; ca.clearValue = bg;
        rp.colorAttachments.Add(ca);
        rhi::DepthStencilAttachment ds{};
        ds.view = m_depthView; ds.depthLoadOp = rhi::LoadOp::Clear; ds.depthStoreOp = rhi::StoreOp::Store;
        ds.depthClearValue = 1.0f;
        rp.depthStencilAttachment = ds;
        rp.label = u8"forward";

        // The depth target starts Undefined each frame (we clear it); move it to the
        // depth-write layout before the pass. (The color target was transitioned to
        // RenderTarget by the host.)
        encoder.TransitionTexture(m_depthTex, rhi::ResourceState::Undefined, rhi::ResourceState::DepthStencilWrite);

        rhi::RenderPassEncoder* pass = encoder.BeginRenderPass(rp);
        if (pass == nullptr) { return; }
        pass->SetViewport(0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height));
        pass->SetScissor(0, 0, width, height);

        for (u32 i = 0; i < count; ++i) {
            const Renderable& r = view.renderables[i];
            const MeshGpu* mesh = m_meshes.GetOrUpload(r.mesh);
            if (mesh == nullptr) { continue; }

            materials::PipelineConfig config = (r.material != nullptr)
                ? r.material->pipeline
                : materials::PipelineConfig::ForOpaqueMesh(u8"forward");
            config.depthFormat = m_depthFormat;

            rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayout, colorFormat);
            if (pso == nullptr) { continue; }

            const u32 dynamicOffset = i * static_cast<u32>(kSlotSize);
            pass->SetPipeline(pso);
            pass->SetBindGroup(0, m_objectBindGroup, Span<const u32>{ &dynamicOffset, 1 });
            pass->SetVertexBuffer(0, mesh->vertexBuffer);
            pass->SetIndexBuffer(mesh->indexBuffer, mesh->indexFormat);
            pass->DrawIndexed(mesh->indexCount);
        }
        pass->End();
    }

private:
    struct ObjectData { Mat4 world; Mat4 viewProj; };          // 128 bytes
    static constexpr u64 kSlotSize = 256;                      // dynamic UBO offset alignment

    bool EnsureObjectBuffer(u32 count) {
        if (count <= m_objectCapacity && m_objectBuffer != nullptr) { return true; }
        if (m_objectBindGroup) { m_device->DestroyBindGroup(m_objectBindGroup); }
        if (m_objectBuffer)    { m_device->DestroyBuffer(m_objectBuffer); }

        rhi::BufferDesc bd{};
        bd.size = static_cast<u64>(count) * kSlotSize;
        bd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryLocation::CpuToGpu;
        bd.label = u8"forward.objects";
        if (!m_device->CreateBuffer(bd, m_objectBuffer).IsOk()) { m_objectBuffer = nullptr; return false; }

        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(m_objectBuffer, 0, sizeof(ObjectData));
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_objectLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, m_objectBindGroup).IsOk()) { m_objectBindGroup = nullptr; return false; }

        m_objectCapacity = count;
        return true;
    }

    bool EnsureDepth(u32 width, u32 height) {
        if (width == 0 || height == 0) { return false; }
        if (m_depthTex != nullptr && m_depthW == width && m_depthH == height) { return true; }
        if (m_depthView) { m_device->DestroyTextureView(m_depthView); }
        if (m_depthTex)  { m_device->DestroyTexture(m_depthTex); }

        rhi::TextureDesc td = rhi::TextureDesc::DepthBuffer(m_depthFormat, width, height, 1, u8"forward.depth");
        if (!m_device->CreateTexture(td, m_depthTex).IsOk()) { m_depthTex = nullptr; return false; }
        rhi::TextureViewDesc vd{};
        vd.format = m_depthFormat; vd.dimension = rhi::TextureViewDimension::Texture2D;
        vd.aspect = rhi::TextureAspect::DepthOnly;
        if (!m_device->CreateTextureView(m_depthTex, vd, m_depthView).IsOk()) { m_depthView = nullptr; return false; }
        m_depthW = width; m_depthH = height;
        return true;
    }

    void Shutdown() {
        m_meshes.Clear();
        if (m_objectBindGroup) { m_device->DestroyBindGroup(m_objectBindGroup); m_objectBindGroup = nullptr; }
        if (m_objectBuffer)    { m_device->DestroyBuffer(m_objectBuffer); m_objectBuffer = nullptr; }
        if (m_depthView)       { m_device->DestroyTextureView(m_depthView); m_depthView = nullptr; }
        if (m_depthTex)        { m_device->DestroyTexture(m_depthTex); m_depthTex = nullptr; }
        if (m_pipelineLayout)  { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_objectLayout)    { m_device->DestroyBindGroupLayout(m_objectLayout); m_objectLayout = nullptr; }
    }

    rhi::Device*                   m_device;
    shaders::ShaderSystem*         m_shaders;
    materials::PipelineStateCache* m_psoCache;
    MeshGpuCache                   m_meshes;

    rhi::BindGroupLayout* m_objectLayout    = nullptr;
    rhi::PipelineLayout*  m_pipelineLayout  = nullptr;
    rhi::Buffer*          m_objectBuffer    = nullptr;
    rhi::BindGroup*       m_objectBindGroup = nullptr;
    u32                   m_objectCapacity  = 0;

    rhi::Texture*      m_depthTex   = nullptr;
    rhi::TextureView*  m_depthView  = nullptr;
    u32                m_depthW = 0, m_depthH = 0;
    rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Depth32Float;
};

} // namespace raptor::render
