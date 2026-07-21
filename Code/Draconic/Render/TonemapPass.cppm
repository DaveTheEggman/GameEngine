/// Draconic::Render - the `:tonemap` partition.
///
/// The HDR resolve: the forward pass renders linear HDR into a transient (RGBA16F); this fullscreen
/// pass reads it, applies exposure + a tonemap operator + the display OETF, and writes the LDR
/// target. Keeps the renderer in a strict linear working space (docs/design/renderer.md §12) - the
/// foundation IBL/post are designed against. CM1a uses a trivial clamp; CM1b swaps in AgX.

module;
#include "Core/Prelude.h"

export module draconic.render:tonemap;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :tonemap_shaders;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Tonemaps an HDR transient into an LDR target. Owns the fullscreen pipeline + per-(view,frame)
// bind groups over the (transient) HDR view. Declared once per view, after that view's forward pass.
class TonemapPass {
public:
    TonemapPass(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}

    ~TonemapPass() { Shutdown(); }
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"tonemap", shaders::ShaderStage::Vertex,   TonemapVS());
        m_shaders->RegisterSource(u8"tonemap", shaders::ShaderStage::Fragment, TonemapPS());

        // set 0: HDR (t0) + bloom (t1) + AO (t2), all sampled, + a linear sampler (s0).
        rhi::BindGroupLayoutEntry hdrEntry   = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry bloomEntry = rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry aoEntry    = rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry sampEntry  = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry entries[] = { hdrEntry, bloomEntry, aoEntry, sampEntry };
        rhi::BindGroupLayoutDesc ld{};
        ld.entries = Span<const rhi::BindGroupLayoutEntry>{ entries, 4 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(f32) * 9;   // exposure + bloom + uvScale.xy + uvOffset.xy + aoStrength + debugShowAo + operator
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"tonemap.bloomSampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat HdrFormat() const noexcept { return kHdrFormat; }

    // Declare the tonemap pass: read `hdr`, write `ldr` (clearColor decides clear vs load), into the
    // view's viewport sub-rect. The execute builds/binds the HDR bind group (the view is a transient,
    // resolved at execute time) and draws a fullscreen triangle.
    void DeclareTonemap(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr, rendergraph::RGHandle bloom, rendergraph::RGHandle ao,
                        rendergraph::RGHandle ldr, bool clearColor, const rhi::ClearColor& clear, rhi::TextureFormat ldrFormat,
                        i32 vpX, i32 vpY, u32 vpW, u32 vpH, u32 frameIndex, u32 viewIndex, f32 exposure = 1.0f, f32 bloomIntensity = 0.0f,
                        Float2 uvScale = Float2{ 1, 1 }, Float2 uvOffset = Float2{ 0, 0 }, f32 aoStrength = 0.0f, bool debugShowAo = false,
                        bool agx = true) {
        rhi::RenderPipeline* pipeline = EnsurePipeline(ldrFormat);
        if (pipeline == nullptr) { return; }
        const u32 slot = (viewIndex % kMaxViews) * m_framesInFlight + (frameIndex % m_framesInFlight);
        const f32 push[9] = { exposure, bloomIntensity, uvScale.x, uvScale.y, uvOffset.x, uvOffset.y, aoStrength, debugShowAo ? 1.0f : 0.0f, agx ? 1.0f : 0.0f };

        const rhi::LoadOp load = clearColor ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        graph.AddRenderPass(u8"tonemap",
            [this, &graph, hdr, bloom, ao, ldr, load, clear, vpX, vpY, vpW, vpH, pipeline, slot, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, ldr, load, rhi::StoreOp::Store, clear);
                b.ReadTexture(hdr);
                b.ReadTexture(bloom);
                b.ReadTexture(ao);
                b.SetViewport(vpX, vpY, vpW, vpH);
                b.NeverCull();
                b.SetExecute([this, &graph, hdr, bloom, ao, pipeline, slot, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(slot, graph.GetTextureView(hdr), graph.GetTextureView(bloom), graph.GetTextureView(ao),
                                                         graph.GetTextureGeneration(hdr) ^ (graph.GetTextureGeneration(bloom) * 1099511628211ull)
                                                         ^ (graph.GetTextureGeneration(ao) * 14695981039346656037ull));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(pipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(push), push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
    }

private:
    static constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr u32 kMaxFramesInFlight = 8;
    static constexpr u32 kMaxViews = 8;
    static constexpr u32 kMaxSlots = kMaxViews * kMaxFramesInFlight;

    // Build the fullscreen pipeline for `fmt` (rebuilt if the LDR target format changes - usually one).
    rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat fmt) {
        if (m_pipeline != nullptr && m_pipelineFormat == fmt) { return m_pipeline; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"tonemap", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"tonemap", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }

        rhi::ColorTargetState color{}; color.format = fmt;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };

        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"tonemap";
        if (!m_device->CreateRenderPipeline(pd, m_pipeline).IsOk()) { m_pipeline = nullptr; return nullptr; }
        m_pipelineFormat = fmt;
        return m_pipeline;
    }

    // One bind group per (view, frame) slot over its HDR transient view. Rebuilt when the transient's
    // GENERATION changes (the graph stamps a fresh id whenever a different physical texture backs the
    // transient - e.g. on resize). Pointer identity alone is unsafe: a freed view address can be reused
    // by the new allocation, leaving the cached bind group pointing at a destroyed texture.
    rhi::BindGroup* EnsureBindGroup(u32 slot, rhi::TextureView* hdrView, rhi::TextureView* bloomView, rhi::TextureView* aoView, u64 generation) {
        if (slot >= kMaxSlots || hdrView == nullptr || bloomView == nullptr || aoView == nullptr) { return nullptr; }
        if (m_bindGroups[slot] != nullptr && m_bgViews[slot] == hdrView && m_bgBloom[slot] == bloomView && m_bgAo[slot] == aoView && m_bgGen[slot] == generation) {
            return m_bindGroups[slot];
        }
        if (m_bindGroups[slot] != nullptr) { m_device->DestroyBindGroup(m_bindGroups[slot]); m_bindGroups[slot] = nullptr; }
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::TextureEntry(hdrView),
            rhi::BindGroupEntry::TextureEntry(bloomView),
            rhi::BindGroupEntry::TextureEntry(aoView),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_layout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 4 };
        if (!m_device->CreateBindGroup(bgd, m_bindGroups[slot]).IsOk()) { m_bindGroups[slot] = nullptr; return nullptr; }
        m_bgViews[slot] = hdrView;
        m_bgBloom[slot] = bloomView;
        m_bgAo[slot]    = aoView;
        m_bgGen[slot]   = generation;
        return m_bindGroups[slot];
    }

    void Shutdown() {
        for (u32 i = 0; i < kMaxSlots; ++i) {
            if (m_bindGroups[i] != nullptr) { m_device->DestroyBindGroup(m_bindGroups[i]); m_bindGroups[i] = nullptr; }
        }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    u32                    m_framesInFlight = 2;

    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::TextureFormat     m_pipelineFormat = rhi::TextureFormat::Undefined;

    rhi::Sampler*          m_sampler = nullptr;       // linear-clamp, for the bloom composite
    rhi::BindGroup*        m_bindGroups[kMaxSlots] = {};
    rhi::TextureView*      m_bgViews[kMaxSlots] = {};
    rhi::TextureView*      m_bgBloom[kMaxSlots] = {};
    rhi::TextureView*      m_bgAo[kMaxSlots] = {};
    u64                    m_bgGen[kMaxSlots] = {};   // transient generation the cached BG was built for
};

} // namespace draconic::render
