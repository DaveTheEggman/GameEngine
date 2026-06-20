// VG Sandbox — end-to-end verification of the VG stack (raptor.vg + .renderer +
// .svg + fonts). Adapted from Sedulous Samples/VG/VGSandbox. Brings up a window +
// device + swapchain via the sample framework, compiles the vg shaders, and each
// frame draws a NanoVG-style demo (shapes, gradients, strokes, dashes, an image,
// an SVG badge, and text) through VGContext -> VGBatch -> VGRenderer -> RHI.

#include <new>
#include <cstdio>

import raptor.core;
import raptor.rhi;
import raptor.rhi.vk;
import raptor.shaders;
import raptor.samples.framework;
import raptor.image;
import raptor.fonts;
import raptor.fonts.ttf;
import raptor.vg;
import raptor.vg.renderer;
import raptor.vg.svg;

using namespace raptor::core;
namespace sf = raptor::samples::framework;
namespace rhi = raptor::rhi;
namespace ds = raptor::shaders;
namespace img = raptor::image;
namespace fonts = raptor::fonts;
namespace vg = raptor::vg;
namespace vgr = raptor::vg::renderer;
namespace svg = raptor::vg::svg;

namespace
{
    constexpr const char8_t kVertSrc[] = u8R"(
#pragma pack_matrix(row_major)
cbuffer VGUniforms : register(b0) { float4x4 Projection; };
struct VSInput { float2 Position:TEXCOORD0; float2 TexCoord:TEXCOORD1; float4 Color:TEXCOORD2; float Coverage:TEXCOORD3; };
struct VSOutput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
VSOutput main(VSInput input) {
    VSOutput o;
    o.Position = mul(float4(input.Position, 0.0, 1.0), Projection);
    o.TexCoord = input.TexCoord; o.Color = input.Color; o.Coverage = input.Coverage;
    return o;
}
)";

    constexpr const char8_t kFragSrc[] = u8R"(
struct PSInput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
Texture2D VGTexture : register(t0);
SamplerState VGSampler : register(s0);
float4 main(PSInput input) : SV_Target {
    float4 texColor = VGTexture.Sample(VGSampler, input.TexCoord);
    float4 result = texColor * input.Color;
    result.a *= input.Coverage;
    return result;
}
)";

#ifndef RAPTOR_VG_FONT_PATH
#define RAPTOR_VG_FONT_PATH ""
#endif
}

class VGSandbox : public sf::SampleApp
{
public:
    using sf::SampleApp::SampleApp;
    StringView Title() const override { return u8"VG Sandbox"; }
    u32 BufferCount() const override { return kFrames; }

protected:
    Status OnInit() override;
    void   OnRender() override;
    void   OnShutdown() override;

private:
    static constexpr u32 kFrames = 2;

    void DrawScene(vg::VGContext& ctx, f32 w, f32 h, f32 t);

    ds::Compiler*        m_compiler = nullptr;
    rhi::ShaderModule*   m_vs = nullptr;
    rhi::ShaderModule*   m_fs = nullptr;
    rhi::CommandPool*    m_pool = nullptr;
    rhi::Fence*          m_fence = nullptr;
    u64                  m_fenceVal = 0;
    u32                  m_frameIndex = 0;

    UniquePtr<fonts::TrueTypeFontService> m_fontService;
    UniquePtr<vg::VGContext>  m_vg;
    vgr::VGRenderer           m_renderer;
    img::OwnedImageData       m_checker;
    svg::SVGDocument          m_badge;
    bool                      m_hasBadge = false;
};

Status VGSandbox::OnInit()
{
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kVertSrc, ds::ShaderStage::Vertex,   u8"main", u8"vg.vert", m_vs) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kFragSrc, ds::ShaderStage::Fragment, u8"main", u8"vg.frag", m_fs) != ErrorCode::Ok) return ErrorCode::Unknown;

    if (!m_renderer.Initialize(*m_device, *m_vs, *m_fs, m_swapChain->Format(), static_cast<i32>(kFrames)).IsOk())
        return ErrorCode::Unknown;

    if (m_device->CreateCommandPool(rhi::QueueType::Graphics, m_pool) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != ErrorCode::Ok) return ErrorCode::Unknown;

    // Fonts (optional — text is skipped if the font can't be loaded).
    m_fontService = MakeUnique<fonts::TrueTypeFontService>(DefaultAllocator());
    const StringView fontPath(reinterpret_cast<const utf8char*>(RAPTOR_VG_FONT_PATH));
    if (!fontPath.IsEmpty())
        (void)m_fontService->LoadFont(u8"Roboto", fontPath);

    m_vg = MakeUnique<vg::VGContext>(DefaultAllocator(), m_fontService.Get());

    // A 128x128 checkerboard image for DrawImage.
    {
        img::Image checker = img::Image::CreateCheckerboard(128, Color32{ 230, 230, 230, 255 }, Color32{ 60, 60, 70, 255 }, 16);
        const Span<const u8> px = checker.PixelData();
        Array<u8> copy; copy.Resize(px.Size());
        if (px.Size() > 0) MemCopy(copy.Data(), px.Data(), px.Size());
        m_checker = img::OwnedImageData(checker.Width(), checker.Height(), img::PixelFormat::RGBA8, Move(copy));
    }

    // An SVG badge.
    {
        const StringView svgSrc =
            u8"<svg viewBox=\"0 0 100 100\">"
            u8"<circle cx=\"50\" cy=\"50\" r=\"45\" fill=\"#2A6BC0\" stroke=\"#1A4A90\" stroke-width=\"3\"/>"
            u8"<circle cx=\"50\" cy=\"50\" r=\"30\" fill=\"none\" stroke=\"#4A9AFF\" stroke-width=\"1.5\" opacity=\"0.6\"/>"
            u8"</svg>";
        Result<svg::SVGDocument> r = svg::SVGLoader::Load(svgSrc);
        if (r.HasValue()) { m_badge = Move(r.Value()); m_hasBadge = true; }
    }

    return ErrorCode::Ok;
}

void VGSandbox::DrawScene(vg::VGContext& ctx, f32 w, f32 h, f32 t)
{
    const Color32 white = Color32::White;

    // Filled shapes + gradients (left column).
    ctx.FillRoundedRect(Rect{ 20, 20, 200, 120 }, 16.0f, Color32{ 40, 44, 52, 255 });

    vg::VGLinearGradientFill grad(Vec2{ 40, 40 }, Vec2{ 200, 40 });
    grad.AddStop(0.0f, ToColor(Color32{ 255, 90, 90, 255 }));
    grad.AddStop(1.0f, ToColor(Color32{ 90, 140, 255, 255 }));
    {
        vg::PathBuilder pb;
        vg::ShapeBuilder::BuildRoundedRect(Rect{ 40, 40, 160, 36 }, vg::CornerRadii(8.0f), pb);
        ctx.FillPath(pb.ToPath(), grad);
    }

    ctx.FillCircle(Vec2{ 70, 110 }, 22.0f, Color32{ 80, 200, 120, 255 });
    ctx.FillStar(Vec2{ 140, 110 }, 24.0f, 11.0f, 5, Color32{ 255, 200, 60, 255 });

    // Strokes: caps/joins + a dashed, animated line (middle).
    ctx.StrokeRoundedRect(Rect{ 240, 20, 200, 120 }, 16.0f, Color32{ 120, 130, 150, 255 }, 2.0f);
    {
        vg::PathBuilder pb;
        pb.MoveTo(260, 60); pb.LineTo(320, 100); pb.LineTo(380, 50); pb.LineTo(420, 110);
        const f32 dash[2] = { 12.0f, 8.0f };
        vg::StrokeStyle style(4.0f, vg::VGLineCap::Round, vg::VGLineJoin::Round);
        style.dashOffset = -t * 30.0f;
        ctx.StrokePath(pb.ToPath(), Color32{ 90, 220, 220, 255 }, style, Span<const f32>(dash, 2));
    }

    // Image (right).
    ctx.DrawImage(&m_checker, Rect{ 460, 20, 120, 120 });

    // SVG badge (animated rotation).
    if (m_hasBadge)
    {
        ctx.PushState();
        ctx.Translate(660, 80);
        ctx.Rotate(t * 0.5f);
        ctx.Translate(-50, -50);
        svg::SVGRenderer::Render(ctx, m_badge, Rect{ 0, 0, 100, 100 });
        ctx.PopState();
    }

    // Text.
    if (m_fontService && !StringView(reinterpret_cast<const utf8char*>(RAPTOR_VG_FONT_PATH)).IsEmpty())
    {
        ctx.DrawText(u8"Raptor VG Sandbox", 28.0f, Vec2{ 20.0f, h - 60.0f }, white);
        ctx.DrawText(u8"shapes  gradients  strokes  dashes  images  svg  text", 16.0f, Vec2{ 20.0f, h - 30.0f }, Color32{ 180, 190, 210, 255 });
    }

    (void)w;
}

void VGSandbox::OnRender()
{
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != ErrorCode::Ok) return;

    const f32 w = static_cast<f32>(m_width);
    const f32 h = static_cast<f32>(m_height);

    m_vg->Clear();
    DrawScene(*m_vg, w, h, m_totalTime);
    vg::VGBatch& batch = m_vg->GetBatch();

    m_renderer.BeginFrame(static_cast<i32>(m_frameIndex));
    const vgr::VGRenderSlice slice = m_renderer.Prepare(batch, static_cast<i32>(m_frameIndex), m_width, m_height);

    m_pool->Reset();
    rhi::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != ErrorCode::Ok || enc == nullptr) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), rhi::ResourceState::Undefined, rhi::ResourceState::RenderTarget);

    rhi::ColorAttachment ca{};
    ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store;
    ca.clearValue = rhi::ClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    rhi::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);

    rhi::RenderPassEncoder* rp = enc->BeginRenderPass(rpd);
    m_renderer.Render(*rp, m_width, m_height, static_cast<i32>(m_frameIndex), slice);
    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), rhi::ResourceState::RenderTarget, rhi::ResourceState::Present);

    rhi::CommandBuffer* cb = enc->Finish();
    ++m_fenceVal;
    rhi::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<rhi::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);

    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);

    m_frameIndex = (m_frameIndex + 1) % kFrames;
}

void VGSandbox::OnShutdown()
{
    if (m_device) m_device->WaitIdle();
    m_renderer.Dispose();
    m_vg.Reset();
    m_fontService.Reset();
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool)  m_device->DestroyCommandPool(m_pool);
    if (m_fs)    m_device->DestroyShaderModule(m_fs);
    if (m_vs)    m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { VGSandbox app; return app.Run(argc, argv); }
