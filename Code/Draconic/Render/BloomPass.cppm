/// Draconic::Render - the `:bloom` partition.
///
/// HDR bloom via a downsample/upsample pyramid (Jimenez "Next Generation Post Processing in Call of
/// Duty" - 13-tap downsample + 9-tap tent upsample, additive). The first downsample soft-knee-
/// thresholds + Karis-averages to keep fireflies out. The pyramid is built from render-graph
/// transients (sized per view, so no resize handling); the result is composited additively by the
/// tonemap pass. Runs on the linear HDR scene, before tonemap.

module;
#include "Core/Prelude.h"

export module draconic.render:bloom;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :bloom_shaders;   // BloomVS()/BloomCommon()/BloomDownPS()/BloomUpPS() - HLSL source in BloomShaders.cppm

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Builds the bloom pyramid from an HDR input; returns the mip-0 (half-res) accumulated bloom handle,
// which the tonemap composites. Owns the down/up pipelines + sampler; the pyramid is graph transients.
class BloomPass {
public:
    static constexpr rhi::TextureFormat kBloomFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr u32 kMaxLevels = 7;

    BloomPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~BloomPass() { Shutdown(); }
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"bloom_ds", shaders::ShaderStage::Vertex,   BloomVS());
        m_shaders->RegisterSource(u8"bloom_ds", shaders::ShaderStage::Fragment, Concat(BloomCommon(), BloomDownPS()));
        m_shaders->RegisterSource(u8"bloom_us", shaders::ShaderStage::Vertex,   BloomVS());
        m_shaders->RegisterSource(u8"bloom_us", shaders::ShaderStage::Fragment, Concat(BloomCommon(), BloomUpPS()));

        rhi::BindGroupLayoutEntry tex  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry samp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry e[] = { tex, samp };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ e, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(BloomPush);
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"bloom.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        m_downPipeline = MakePipeline(u8"bloom_ds", /*additive*/ false);
        m_upPipeline   = MakePipeline(u8"bloom_us", /*additive*/ true);
        if (m_downPipeline == nullptr || m_upPipeline == nullptr) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat Format() const noexcept { return kBloomFormat; }

    // Declare the pyramid for one view's HDR input. Returns the accumulated bloom (mip 0, half-res), or
    // an invalid handle if the view is too small. threshold/knee gate what blooms (soft-knee prefilter).
    [[nodiscard]] rendergraph::RGHandle DeclareBloom(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr,
                                                     u32 vpW, u32 vpH, f32 threshold, f32 knee) {
        if (vpW < 4 || vpH < 4) { return {}; }
        // Level count: halve until a small floor, capped.
        u32 levels = 1;
        for (u32 w = vpW / 2, h = vpH / 2; levels < kMaxLevels && w > 8 && h > 8; w /= 2, h /= 2) { ++levels; }

        rendergraph::RGHandle chain[kMaxLevels] = {};
        u32 lw[kMaxLevels] = {}, lh[kMaxLevels] = {};
        u32 w = vpW, h = vpH;
        for (u32 i = 0; i < levels; ++i) {
            w = Max(1u, w / 2); h = Max(1u, h / 2);
            lw[i] = w; lh[i] = h;
            chain[i] = graph.CreateTransient(u8"bloom.mip", rendergraph::RGTextureDesc(kBloomFormat, w, h));
        }

        // Downsample: hdr -> mip0 (threshold), mip(i-1) -> mip(i).
        for (u32 i = 0; i < levels; ++i) {
            const rendergraph::RGHandle src = (i == 0) ? hdr : chain[i - 1];
            const u32 srcW = (i == 0) ? vpW : lw[i - 1];
            const u32 srcH = (i == 0) ? vpH : lh[i - 1];
            BloomPush push{};
            push.srcTexel = Float2{ 1.0f / static_cast<f32>(srcW), 1.0f / static_cast<f32>(srcH) };
            push.threshold = threshold; push.knee = knee; push.firstPass = (i == 0) ? 1 : 0;
            const rendergraph::RGHandle dst = chain[i];
            const u32 dw = lw[i], dh = lh[i];
            graph.AddRenderPass(u8"bloom.down", [this, &graph, src, dst, dw, dh, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, dst, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
                b.ReadTexture(src);
                b.SetViewport(0, 0, dw, dh);
                b.NeverCull();
                b.SetExecute([this, &graph, src, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(src), graph.GetTextureGeneration(src));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(m_downPipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BloomPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }

        // Upsample (additive): mip(i+1) -> add into mip(i), coarsest to finest.
        for (i32 i = static_cast<i32>(levels) - 2; i >= 0; --i) {
            const rendergraph::RGHandle src = chain[i + 1];
            const rendergraph::RGHandle dst = chain[i];
            BloomPush push{};
            push.srcTexel = Float2{ 1.0f / static_cast<f32>(lw[i + 1]), 1.0f / static_cast<f32>(lh[i + 1]) };
            const u32 dw = lw[i], dh = lh[i];
            graph.AddRenderPass(u8"bloom.up", [this, &graph, src, dst, dw, dh, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, dst, rhi::LoadOp::Load, rhi::StoreOp::Store, rhi::ClearColor::Black());   // additive blend
                b.ReadTexture(src);
                b.SetViewport(0, 0, dw, dh);
                b.NeverCull();
                b.SetExecute([this, &graph, src, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(src), graph.GetTextureGeneration(src));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(m_upPipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BloomPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }
        return chain[0];
    }

private:
    struct BloomPush { Float2 srcTexel{}; f32 threshold = 1.0f; f32 knee = 0.5f; i32 firstPass = 0; f32 pad0 = 0, pad1 = 0, pad2 = 0; };

    static String Concat(StringView a, StringView b) { String s(a); s.Append(b); return s; }

    rhi::RenderPipeline* MakePipeline(StringView name, bool additive) {
        rhi::ShaderModule* vs = m_shaders->GetVariant(name, shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(name, shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        rhi::ColorTargetState color{}; color.format = kBloomFormat;
        if (additive) { color.blend = rhi::BlendState::Additive(); }
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = name;
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // Bind group over a (transient) source view, cached by view pointer + generation. Transients are
    // pooled with stable generations across frames, so this stabilizes; a resize bumps the generation.
    rhi::BindGroup* EnsureBindGroup(rhi::TextureView* view, u64 generation) {
        if (view == nullptr) { return nullptr; }
        if (Entry* e = m_bindGroups.Find(view)) {
            if (e->gen == generation && e->bg != nullptr) { return e->bg; }
            if (e->bg != nullptr) { m_device->DestroyBindGroup(e->bg); e->bg = nullptr; }
        }
        rhi::BindGroupEntry ent[] = { rhi::BindGroupEntry::TextureEntry(view), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_bindGroups.InsertOrAssign(view, Entry{ bg, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        if (m_downPipeline != nullptr) { m_device->DestroyRenderPipeline(m_downPipeline); m_downPipeline = nullptr; }
        if (m_upPipeline != nullptr) { m_device->DestroyRenderPipeline(m_upPipeline); m_upPipeline = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; u64 gen = 0; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_downPipeline = nullptr;
    rhi::RenderPipeline*   m_upPipeline = nullptr;
    rhi::Sampler*          m_sampler = nullptr;
    HashMap<rhi::TextureView*, Entry> m_bindGroups;
};

} // namespace draconic::render
