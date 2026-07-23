/// Draconic::Render - the `:ao` partition.
///
/// Ambient occlusion. Two interchangeable generators feed one shared pipeline:
///   - GTAO: Ground-Truth AO (Jimenez horizon integration over screen-space slices).
///   - SSAO: classic hemisphere-kernel occlusion (Crysis-style), ported from Sedulous.
/// Both consume the opaque depth + octahedral view-normal (G-buffer), write an R8 AO transient,
/// then share the SAME depth-aware bilateral blur, the SAME apply pass (multiply into the HDR before
/// TAA so the resolve stabilizes it), the SAME bind-group cache, and the SAME debug channels. The two
/// modes are mutually exclusive (AoMode). All targets are render-graph transients (sized per view).

module;
#include "Core/Prelude.h"

export module draconic.render:ao;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :ao_shaders; // AoVS()/AoCommon()/GtaoGenPS()/SsaoGenPS()/AoBlurPS()/AoApplyPS() - HLSL in AoShaders.cppm

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render
{

    // Which AO generator to run (mutually exclusive - both write the same AO buffer).
    enum class AoMode : u32
    {
        Off = 0,
        GTAO = 1,
        SSAO = 2
    };

    // Owns both AO generators + the shared blur/apply pipelines. Produces an AO transient (R8) applied to
    // the HDR before TAA.
    class AoPass
    {
    public:
        static constexpr rhi::TextureFormat kAoFormat = rhi::TextureFormat::R8Unorm;

        AoPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
            : m_device(&device), m_shaders(&shaders)
        {
        }
        ~AoPass() { Shutdown(); }
        AoPass(const AoPass&) = delete;
        AoPass& operator=(const AoPass&) = delete;

        Status Initialize()
        {
            m_shaders->RegisterSource(u8"ao_gtao", shaders::ShaderStage::Vertex, AoVS());
            m_shaders->RegisterSource(u8"ao_gtao", shaders::ShaderStage::Fragment,
                                      Concat(AoCommon(), GtaoGenPS()));
            m_shaders->RegisterSource(u8"ao_ssao", shaders::ShaderStage::Vertex, AoVS());
            m_shaders->RegisterSource(u8"ao_ssao", shaders::ShaderStage::Fragment,
                                      Concat(AoCommon(), SsaoGenPS()));
            m_shaders->RegisterSource(u8"ao_blur", shaders::ShaderStage::Vertex, AoVS());
            m_shaders->RegisterSource(u8"ao_blur", shaders::ShaderStage::Fragment, AoBlurPS());
            m_shaders->RegisterSource(u8"ao_apply", shaders::ShaderStage::Vertex, AoVS());
            m_shaders->RegisterSource(u8"ao_apply", shaders::ShaderStage::Fragment, AoApplyPS());

            // Shared bind-group layout: two sampled textures (t0, t1) + a sampler (s0).
            rhi::BindGroupLayoutEntry ge[] = {
                rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
            };
            rhi::BindGroupLayoutDesc gld{};
            gld.entries = Span<const rhi::BindGroupLayoutEntry>{ge, 3};
            if (!m_device->CreateBindGroupLayout(gld, m_layout).IsOk())
            {
                return Status{ErrorCode::Unknown};
            }

            m_gtaoLayout = MakePipelineLayout(sizeof(GtaoPushC));
            m_ssaoLayout = MakePipelineLayout(sizeof(SsaoPushC));
            m_blurLayout = MakePipelineLayout(sizeof(BlurPushC));
            m_applyLayout = MakePipelineLayout(sizeof(ApplyPushC));
            if (m_gtaoLayout == nullptr || m_ssaoLayout == nullptr || m_blurLayout == nullptr ||
                m_applyLayout == nullptr)
            {
                return Status{ErrorCode::Unknown};
            }

            rhi::SamplerDesc ss{};
            ss.minFilter = rhi::FilterMode::Nearest;
            ss.magFilter = rhi::FilterMode::Nearest;
            ss.addressU = rhi::AddressMode::ClampToEdge;
            ss.addressV = rhi::AddressMode::ClampToEdge;
            ss.addressW = rhi::AddressMode::ClampToEdge;
            ss.label = u8"ao.sampler";
            if (!m_device->CreateSampler(ss, m_sampler).IsOk())
            {
                return Status{ErrorCode::Unknown};
            }

            m_gtaoPipeline = MakePipeline(u8"ao_gtao", m_gtaoLayout);
            m_ssaoPipeline = MakePipeline(u8"ao_ssao", m_ssaoLayout);
            m_blurPipeline = MakePipeline(u8"ao_blur", m_blurLayout);
            m_applyPipeline = MakePipeline(u8"ao_apply", m_applyLayout, kHdrFormat);
            if (m_gtaoPipeline == nullptr || m_ssaoPipeline == nullptr ||
                m_blurPipeline == nullptr || m_applyPipeline == nullptr)
            {
                return Status{ErrorCode::Unknown};
            }
            return Status{};
        }

        [[nodiscard]] rhi::TextureFormat AoFormat() const noexcept { return kAoFormat; }

        // Declare AO for one view; returns the (blurred) AO handle, or invalid if mode is Off. invProj =
        // inverse camera projection, proj = camera projection (GTAO uses proj(1,1); SSAO uses proj(0,0)/(1,1)).
        [[nodiscard]] rendergraph::RGHandle DeclareAo(rendergraph::RenderGraph& graph,
                                                      rendergraph::RGHandle depth,
                                                      rendergraph::RGHandle normal, u32 w, u32 h,
                                                      const Float4x4& invProj, const Float4x4& proj,
                                                      f32 radius, f32 intensity, u32 frameIndex,
                                                      AoMode mode, i32 debugMode = 0)
        {
            if (mode == AoMode::Off || w == 0 || h == 0)
            {
                return {};
            }
            Tick(frameIndex);
            const Float2 texel{1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h)};
            const rendergraph::RGHandle aoRaw =
                graph.CreateTransient(u8"ao.raw", rendergraph::RGTextureDesc(kAoFormat, w, h));
            const rendergraph::RGHandle aoTmp =
                graph.CreateTransient(u8"ao.tmp", rendergraph::RGTextureDesc(kAoFormat, w, h));
            const rendergraph::RGHandle aoOut =
                graph.CreateTransient(u8"ao.ao", rendergraph::RGTextureDesc(kAoFormat, w, h));

            PushBuf push{};
            rhi::RenderPipeline* pipeline = nullptr;
            if (mode == AoMode::GTAO)
            {
                GtaoPushC gp{};
                gp.invProj = invProj;
                gp.texelSize = texel;
                gp.radius = radius;
                gp.intensity = intensity;
                gp.projScaleY = proj(1, 1);
                gp.frameMod = static_cast<i32>(frameIndex & 63u);
                gp.debugMode = debugMode;
                push = PushBuf::From(gp);
                pipeline = m_gtaoPipeline;
            }
            else
            {
                SsaoPushC sp{};
                sp.invProj = invProj;
                sp.texelSize = texel;
                sp.projXX = proj(0, 0);
                sp.projYY = proj(1, 1);
                sp.jitter = Float2{
                    proj(2, 0),
                    proj(
                        2,
                        1)}; // NDC jitter (proj z-row) so back-projection matches the jittered depth
                sp.radius = radius;
                sp.intensity = intensity;
                sp.bias = 0.05f;
                sp.sampleCount = 16;
                sp.debugMode = debugMode;
                push = PushBuf::From(sp);
                pipeline = m_ssaoPipeline;
            }

            graph.AddRenderPass(
                u8"ao.gen",
                [this, &graph, depth, normal, aoRaw, w, h, pipeline,
                 push](rendergraph::PassBuilder& b)
                {
                    b.SetColorTarget(0, aoRaw, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                     rhi::ClearColor::White());
                    b.ReadTexture(depth);
                    b.ReadTexture(normal);
                    b.SetViewport(0, 0, w, h);
                    b.NeverCull();
                    b.SetExecute(
                        [this, &graph, depth, normal, pipeline, push](rhi::RenderPassEncoder& rp)
                        {
                            rhi::BindGroup* bg = EnsureBindGroup(
                                graph.GetTextureView(depth), graph.GetTextureView(normal),
                                graph.GetTextureGeneration(depth) ^
                                    (graph.GetTextureGeneration(normal) * 1099511628211ull));
                            if (bg == nullptr)
                            {
                                return;
                            }
                            rp.SetPipeline(pipeline);
                            rp.SetBindGroup(0, bg, Span<const u32>{});
                            rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, push.size,
                                                push.data);
                            rp.Draw(3, 1, 0, 0);
                        });
                });
            // Non-AO debug channels are raw per-pixel values - skip the bilateral blur that would smear them.
            if (debugMode >= 2)
            {
                return aoRaw;
            }
            DeclareBlur(graph, aoRaw, depth, aoTmp, w, h, texel, Float2{1.0f, 0.0f});
            DeclareBlur(graph, aoTmp, depth, aoOut, w, h, texel, Float2{0.0f, 1.0f});
            return aoOut;
        }

        // Multiply the AO into `hdr` (out = hdr * lerp(1, ao, strength)) into a fresh HDR transient, returned.
        // Run BEFORE the TAA resolve so TAA stabilizes the AO (post-TAA application wobbles under jitter).
        [[nodiscard]] rendergraph::RGHandle DeclareApply(rendergraph::RenderGraph& graph,
                                                         rendergraph::RGHandle hdr,
                                                         rendergraph::RGHandle ao, u32 w, u32 h,
                                                         f32 strength)
        {
            if (w == 0 || h == 0)
            {
                return hdr;
            }
            const rendergraph::RGHandle out =
                graph.CreateTransient(u8"ao.applied", rendergraph::RGTextureDesc(kHdrFormat, w, h));
            ApplyPushC ap{};
            ap.strength = strength;
            graph.AddRenderPass(
                u8"ao.apply",
                [this, &graph, hdr, ao, out, w, h, ap](rendergraph::PassBuilder& b)
                {
                    b.SetColorTarget(0, out, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                     rhi::ClearColor::Black());
                    b.ReadTexture(hdr);
                    b.ReadTexture(ao);
                    b.SetViewport(0, 0, w, h);
                    b.NeverCull();
                    b.SetExecute(
                        [this, &graph, hdr, ao, ap](rhi::RenderPassEncoder& rp)
                        {
                            rhi::BindGroup* bg = EnsureBindGroup(
                                graph.GetTextureView(hdr), graph.GetTextureView(ao),
                                graph.GetTextureGeneration(hdr) ^
                                    (graph.GetTextureGeneration(ao) * 1099511628211ull));
                            if (bg == nullptr)
                            {
                                return;
                            }
                            rp.SetPipeline(m_applyPipeline);
                            rp.SetBindGroup(0, bg, Span<const u32>{});
                            rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(ApplyPushC),
                                                &ap);
                            rp.Draw(3, 1, 0, 0);
                        });
                });
            return out;
        }

    private:
        static constexpr rhi::TextureFormat kHdrFormat =
            rhi::TextureFormat::RGBA16Float; // matches the scene HDR
        struct GtaoPushC
        {
            Float4x4 invProj{};
            Float2 texelSize{};
            f32 radius = 0.5f;
            f32 intensity = 1.0f;
            f32 projScaleY = 1.0f;
            i32 frameMod = 0;
            i32 debugMode = 0;
            i32 pad = 0;
        };
        struct SsaoPushC
        {
            Float4x4 invProj{};
            Float2 texelSize{};
            Float2 jitter{};
            f32 projXX = 1.0f;
            f32 projYY = 1.0f;
            f32 radius = 0.5f;
            f32 intensity = 1.0f;
            f32 bias = 0.05f;
            i32 sampleCount = 16;
            i32 debugMode = 0;
        };
        struct BlurPushC
        {
            Float2 dir{};
            Float2 texelSize{};
            f32 depthSigma = 120.0f;
            f32 p0 = 0, p1 = 0, p2 = 0;
        };
        struct ApplyPushC
        {
            f32 strength = 1.0f;
            f32 p0 = 0, p1 = 0, p2 = 0;
        };

        // A fixed byte buffer so a generate push (GTAO or SSAO) can be captured by value into the pass lambda.
        struct PushBuf
        {
            u8 data[128] = {};
            u32 size = 0;
            template <class T>
            static PushBuf From(const T& v)
            {
                static_assert(sizeof(T) <= 128,
                              "AO push exceeds the portable 128-byte push-constant limit");
                PushBuf b;
                b.size = sizeof(T);
                const u8* src = reinterpret_cast<const u8*>(&v);
                for (u32 i = 0; i < b.size; ++i)
                {
                    b.data[i] = src[i];
                }
                return b;
            }
        };

        // Prepend the shared helpers (OctDecode etc. live in kAoCommon) to a generate shader body.
        static String Concat(StringView a, StringView b)
        {
            String s(a);
            s.Append(b);
            return s;
        }

        rhi::PipelineLayout* MakePipelineLayout(usize pushSize)
        {
            rhi::BindGroupLayout* gl[] = {m_layout};
            rhi::PushConstantRange pc{};
            pc.stages = rhi::ShaderStage::Fragment;
            pc.offset = 0;
            pc.size = static_cast<u32>(pushSize);
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{gl, 1};
            pld.pushConstantRanges = Span<const rhi::PushConstantRange>{&pc, 1};
            rhi::PipelineLayout* layout = nullptr;
            if (!m_device->CreatePipelineLayout(pld, layout).IsOk())
            {
                return nullptr;
            }
            return layout;
        }

        void DeclareBlur(rendergraph::RenderGraph& graph, rendergraph::RGHandle ao,
                         rendergraph::RGHandle depth, rendergraph::RGHandle out, u32 w, u32 h,
                         Float2 texel, Float2 dir)
        {
            BlurPushC bp{};
            bp.dir = dir;
            bp.texelSize = texel;
            graph.AddRenderPass(
                u8"ao.blur",
                [this, &graph, ao, depth, out, w, h, bp](rendergraph::PassBuilder& b)
                {
                    b.SetColorTarget(0, out, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                     rhi::ClearColor::White());
                    b.ReadTexture(ao);
                    b.ReadTexture(depth);
                    b.SetViewport(0, 0, w, h);
                    b.NeverCull();
                    b.SetExecute(
                        [this, &graph, ao, depth, bp](rhi::RenderPassEncoder& rp)
                        {
                            rhi::BindGroup* bg = EnsureBindGroup(
                                graph.GetTextureView(ao), graph.GetTextureView(depth),
                                graph.GetTextureGeneration(ao) ^
                                    (graph.GetTextureGeneration(depth) * 14695981039346656037ull));
                            if (bg == nullptr)
                            {
                                return;
                            }
                            rp.SetPipeline(m_blurPipeline);
                            rp.SetBindGroup(0, bg, Span<const u32>{});
                            rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BlurPushC),
                                                &bp);
                            rp.Draw(3, 1, 0, 0);
                        });
                });
        }

        rhi::RenderPipeline* MakePipeline(StringView name, rhi::PipelineLayout* layout,
                                          rhi::TextureFormat fmt = kAoFormat)
        {
            rhi::ShaderModule* vs = m_shaders->GetVariant(name, shaders::ShaderStage::Vertex,
                                                          shaders::ShaderFlags::None);
            rhi::ShaderModule* ps = m_shaders->GetVariant(name, shaders::ShaderStage::Fragment,
                                                          shaders::ShaderFlags::None);
            if (vs == nullptr || ps == nullptr)
            {
                return nullptr;
            }
            rhi::ColorTargetState color{};
            color.format = fmt;
            rhi::FragmentState frag{};
            frag.shader = rhi::ProgrammableStage{ps, u8"main", rhi::ShaderStage::Fragment};
            frag.targets = Span<const rhi::ColorTargetState>{&color, 1};
            rhi::RenderPipelineDesc pd{};
            pd.layout = layout;
            pd.vertex.shader = rhi::ProgrammableStage{vs, u8"main", rhi::ShaderStage::Vertex};
            pd.fragment = frag;
            pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            pd.primitive.cullMode = rhi::CullMode::None;
            pd.label = name;
            rhi::RenderPipeline* p = nullptr;
            if (!m_device->CreateRenderPipeline(pd, p).IsOk())
            {
                return nullptr;
            }
            return p;
        }

        // Advance the deferred-free list once per frame (frees bind groups retired long enough ago to be
        // idle). The graph aliases transients, so a raw-pointer cache thrashes mid-frame - replaced sets go
        // to the retire list (freed after kRetireFrames) instead of being freed while still in-flight.
        void Tick(u32 frameIndex)
        {
            if (frameIndex == m_lastFrame)
            {
                return;
            }
            m_lastFrame = frameIndex;
            usize w = 0;
            for (usize i = 0; i < m_retired.Size(); ++i)
            {
                if (m_retired[i].left <= 1)
                {
                    m_device->DestroyBindGroup(m_retired[i].bg);
                }
                else
                {
                    m_retired[i].left -= 1;
                    m_retired[w++] = m_retired[i];
                }
            }
            m_retired.Resize(w);
        }

        // Two-texture bind group (t0, t1) + sampler, cached by (t0 view, combined generation). Shared by every
        // AO pass (gen depth+normal, blur ao+depth, apply hdr+ao) - same layout.
        rhi::BindGroup* EnsureBindGroup(rhi::TextureView* a, rhi::TextureView* bView,
                                        u64 generation)
        {
            if (a == nullptr || bView == nullptr)
            {
                return nullptr;
            }
            if (Entry* e = m_bindGroups.Find(a))
            {
                if (e->gen == generation && e->b == bView && e->bg != nullptr)
                {
                    return e->bg;
                }
                if (e->bg != nullptr)
                {
                    m_retired.PushBack(Retired{e->bg, kRetireFrames});
                    e->bg = nullptr;
                } // defer-free (in-flight)
            }
            rhi::BindGroupEntry ent[] = {
                rhi::BindGroupEntry::TextureEntry(a),
                rhi::BindGroupEntry::TextureEntry(bView),
                rhi::BindGroupEntry::SamplerEntry(m_sampler),
            };
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_layout;
            bgd.entries = Span<const rhi::BindGroupEntry>{ent, 3};
            rhi::BindGroup* bg = nullptr;
            if (!m_device->CreateBindGroup(bgd, bg).IsOk())
            {
                return nullptr;
            }
            m_bindGroups.InsertOrAssign(a, Entry{bg, bView, generation});
            return bg;
        }

        void Shutdown()
        {
            for (auto& kv : m_bindGroups)
            {
                if (kv.value.bg != nullptr)
                {
                    m_device->DestroyBindGroup(kv.value.bg);
                }
            }
            m_bindGroups.Clear();
            for (auto& r : m_retired)
            {
                m_device->DestroyBindGroup(r.bg);
            }
            m_retired.Clear();
            if (m_gtaoPipeline != nullptr)
            {
                m_device->DestroyRenderPipeline(m_gtaoPipeline);
                m_gtaoPipeline = nullptr;
            }
            if (m_ssaoPipeline != nullptr)
            {
                m_device->DestroyRenderPipeline(m_ssaoPipeline);
                m_ssaoPipeline = nullptr;
            }
            if (m_blurPipeline != nullptr)
            {
                m_device->DestroyRenderPipeline(m_blurPipeline);
                m_blurPipeline = nullptr;
            }
            if (m_applyPipeline != nullptr)
            {
                m_device->DestroyRenderPipeline(m_applyPipeline);
                m_applyPipeline = nullptr;
            }
            if (m_sampler != nullptr)
            {
                m_device->DestroySampler(m_sampler);
                m_sampler = nullptr;
            }
            if (m_gtaoLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_gtaoLayout);
                m_gtaoLayout = nullptr;
            }
            if (m_ssaoLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_ssaoLayout);
                m_ssaoLayout = nullptr;
            }
            if (m_blurLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_blurLayout);
                m_blurLayout = nullptr;
            }
            if (m_applyLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_applyLayout);
                m_applyLayout = nullptr;
            }
            if (m_layout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_layout);
                m_layout = nullptr;
            }
        }

        struct Entry
        {
            rhi::BindGroup* bg = nullptr;
            rhi::TextureView* b = nullptr;
            u64 gen = 0;
        };
        struct Retired
        {
            rhi::BindGroup* bg = nullptr;
            u32 left = 0;
        };
        static constexpr u32 kRetireFrames =
            4; // frames-in-flight headroom before a replaced set is idle

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;
        rhi::BindGroupLayout* m_layout = nullptr;
        rhi::PipelineLayout* m_gtaoLayout = nullptr;
        rhi::PipelineLayout* m_ssaoLayout = nullptr;
        rhi::PipelineLayout* m_blurLayout = nullptr;
        rhi::PipelineLayout* m_applyLayout = nullptr;
        rhi::RenderPipeline* m_gtaoPipeline = nullptr;
        rhi::RenderPipeline* m_ssaoPipeline = nullptr;
        rhi::RenderPipeline* m_blurPipeline = nullptr;
        rhi::RenderPipeline* m_applyPipeline = nullptr;
        rhi::Sampler* m_sampler = nullptr;
        HashMap<rhi::TextureView*, Entry> m_bindGroups;
        Array<Retired> m_retired;
        u32 m_lastFrame = 0xFFFFFFFFu;
    };

} // namespace draconic::render
