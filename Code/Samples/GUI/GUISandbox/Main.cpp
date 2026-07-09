// GUI Sandbox - the first on-screen test of draconic.gui. Builds a small widget tree
// (panel + labels + buttons in a LinearLayout), styles it with CSS via a StyleManager, and
// renders it through the same VG -> VGRenderer -> RHI path as VGSandbox. Input is driven by
// an InputSurface (fullscreen, gated by an InputRouter) polled into the GUI EventDispatcher
// through GuiInputBridge, so hover/click/CSS-transitions are live.

#include <new>

import draconic.core;
import draconic.rhi;
import draconic.rhi.vk;
import draconic.shaders;
import draconic.samples.framework;
import draconic.shell;
import draconic.image;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.vg;
import draconic.vg.renderer;
import draconic.gui;
import draconic.gui.shell;

using namespace draconic::core;
namespace sf = draconic::samples::framework;
namespace rhi = draconic::rhi;
namespace shaders = draconic::shaders;
namespace shell = draconic::shell;
namespace image = draconic::image;
namespace fonts = draconic::fonts;
namespace vg = draconic::vg;
namespace gui = draconic::gui;

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

#ifndef DRACONIC_GUI_FONT_PATH
#define DRACONIC_GUI_FONT_PATH ""
#endif

    const char8_t* kStyleSheet = u8R"(
        button {
            background-color: #3a6ea5;
            padding: 10;
            opacity: 1;
            transition: opacity 0.18s;
        }
        button:hover  { background-color: #4f8fd0; opacity: 0.92; }
        button:active { background-color: #274d78; }
    )";

    inline Color Col(f32 r, f32 g, f32 b, f32 a = 1.0f) { return Color{ r, g, b, a }; }
}

class GUISandbox : public sf::SampleApp
{
public:
    GUISandbox() { m_width = 900; m_height = 620; }
    StringView Title() const override { return u8"GUI Sandbox"; }
    u32 BufferCount() const override { return kFrames; }

protected:
    Status OnInit() override;
    void   OnRender() override;
    void   OnShutdown() override;

private:
    static constexpr u32 kFrames = 2;

    void BuildUI();
    void LoadFontSize(StringView path, f32 pixelHeight);
    [[nodiscard]] bool HasFonts() const
    {
        return !StringView(reinterpret_cast<const utf8char*>(DRACONIC_GUI_FONT_PATH)).IsEmpty();
    }

    // Render plumbing (mirrors VGSandbox).
    shaders::Compiler* m_compiler = nullptr;
    rhi::ShaderModule* m_vs = nullptr;
    rhi::ShaderModule* m_fs = nullptr;
    rhi::CommandPool*  m_pool = nullptr;
    rhi::Fence*        m_fence = nullptr;
    u64                m_fenceVal = 0;
    u32                m_frameIndex = 0;

    UniquePtr<fonts::TrueTypeFontService> m_fontService;
    UniquePtr<vg::VGContext>              m_vg;
    vg::renderer::VGRenderer              m_renderer;
    fonts::CachedFont* m_font = nullptr;
    fonts::CachedFont* m_fontLarge = nullptr;

    // GUI.
    RefPtr<gui::SceneNode>    m_root;
    RefPtr<gui::LinearLayout> m_panel;
    RefPtr<gui::Label>        m_counter;
    gui::StyleManager         m_styles;
    UniquePtr<gui::GuiInputBridge>   m_bridge;
    UniquePtr<shell::InputSurface>   m_surface;
    UniquePtr<shell::InputRouter>    m_router;
    i32 m_clicks = 0;
};

Status GUISandbox::OnInit()
{
    if (shaders::createCompiler(shaders::CompilerDesc{}, m_compiler) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kVertSrc, shaders::ShaderStage::Vertex,   u8"main", u8"vg.vert", m_vs) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kFragSrc, shaders::ShaderStage::Fragment, u8"main", u8"vg.frag", m_fs) != ErrorCode::Ok) return ErrorCode::Unknown;

    if (!m_renderer.Initialize(*m_device, *m_vs, *m_fs, m_swapChain->Format(), static_cast<i32>(kFrames)).IsOk())
        return ErrorCode::Unknown;

    if (m_device->CreateCommandPool(rhi::QueueType::Graphics, m_pool) != ErrorCode::Ok) return ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != ErrorCode::Ok) return ErrorCode::Unknown;

    m_fontService = MakeUnique<fonts::TrueTypeFontService>(DefaultAllocator());
    if (HasFonts())
    {
        const StringView fontPath(reinterpret_cast<const utf8char*>(DRACONIC_GUI_FONT_PATH));
        LoadFontSize(fontPath, 18.0f);
        LoadFontSize(fontPath, 30.0f);
        m_font      = m_fontService->GetFont(u8"Roboto", 18.0f);
        m_fontLarge = m_fontService->GetFont(u8"Roboto", 30.0f);
    }

    m_vg = MakeUnique<vg::VGContext>(DefaultAllocator(), m_fontService.Get());

    BuildUI();
    return ErrorCode::Ok;
}

void GUISandbox::LoadFontSize(StringView path, f32 pixelHeight)
{
    fonts::FontLoadOptions options = fonts::FontLoadOptions::ExtendedLatin();
    options.pixelHeight = pixelHeight;
    (void)m_fontService->LoadFont(u8"Roboto", path, options);
}

void GUISandbox::BuildUI()
{
    m_root = MakeRef<gui::SceneNode>(DefaultAllocator());
    m_root->SetSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });

    // Full-window panel with a dark background, stacking its children vertically.
    m_panel = MakeRef<gui::LinearLayout>(DefaultAllocator());
    m_panel->SetSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
    m_panel->SetPadding(gui::Thickness{ 28.0f });
    m_panel->SetSpacing(16.0f);
    m_panel->SetBackground(MakeRef<gui::RectangleDrawable>(DefaultAllocator(), Col(0.11f, 0.12f, 0.15f)));
    m_root->AddChild(m_panel.Get());

    auto title = MakeRef<gui::Label>(DefaultAllocator());
    title->SetSize(Float2{ 600.0f, 46.0f });
    title->SetText(u8"Draconic GUI Sandbox");
    title->SetFont(m_fontLarge);
    title->SetTextColor(Col(0.92f, 0.94f, 0.98f));
    m_panel->AddChild(title.Get());

    m_counter = MakeRef<gui::Label>(DefaultAllocator());
    m_counter->SetSize(Float2{ 500.0f, 28.0f });
    m_counter->SetText(u8"Clicks: 0");
    m_counter->SetFont(m_font);
    m_counter->SetTextColor(Col(0.75f, 0.82f, 0.9f));
    m_panel->AddChild(m_counter.Get());

    // A row of buttons.
    auto row = MakeRef<gui::LinearLayout>(DefaultAllocator());
    row->SetOrientation(gui::Orientation::Horizontal);
    row->SetSize(Float2{ static_cast<f32>(m_width) - 56.0f, 50.0f });
    row->SetSpacing(14.0f);
    m_panel->AddChild(row.Get());

    const char8_t* labels[3] = { u8"Click me", u8"Hover me", u8"Styled" };
    for (i32 i = 0; i < 3; ++i)
    {
        auto btn = MakeRef<gui::Button>(DefaultAllocator());
        btn->SetSize(Float2{ 170.0f, 46.0f });
        btn->SetText(labels[i]);
        btn->SetFont(m_font);
        btn->SetTextColor(Col(1.0f, 1.0f, 1.0f));
        row->AddChild(btn.Get());
        if (i == 0)
        {
            gui::Label* counter = m_counter.Get();
            i32* clicks = &m_clicks;
            btn->SetOnClick([counter, clicks]()
            {
                ++(*clicks);
                char8_t buf[32] = u8"Clicks: ";
                // tiny int -> ascii (values are small)
                i32 n = *clicks; usize pos = 8;
                char8_t digits[12]; usize dc = 0;
                if (n == 0) digits[dc++] = u8'0';
                while (n > 0) { digits[dc++] = static_cast<char8_t>(u8'0' + (n % 10)); n /= 10; }
                for (usize k = 0; k < dc; ++k) buf[pos++] = digits[dc - 1 - k];
                buf[pos] = 0;
                counter->SetText(StringView(buf));
            });
        }
    }

    m_styles.SetStyleSheet(gui::CSSParser::Parse(StringView(kStyleSheet)));
    m_bridge = MakeUnique<gui::GuiInputBridge>(DefaultAllocator(), m_root->GetEventDispatcher());
}

void GUISandbox::OnRender()
{
    // Input: a fullscreen InputSurface, gated by a router, polled into the dispatcher.
    if (m_shell != nullptr && m_shell->Input() != nullptr && m_window != nullptr)
    {
        shell::IInputManager& input = *m_shell->Input();
        const Rectangle region{ 0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height) };
        if (!m_surface)
        {
            const ContentFit fit{ region, Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) }, FitMode::Stretch };
            m_surface = MakeUnique<shell::InputSurface>(DefaultAllocator(), &input, m_window->Id(), fit);
            m_router = MakeUnique<shell::InputRouter>(DefaultAllocator(), &input);
            m_router->AddSurface(m_surface.Get());
        }
        m_surface->SetRegion(region);
        m_surface->SetContentSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
        m_router->Update();
        m_bridge->PumpFromSurface(*m_surface);
    }

    // Advance + style the tree.
    m_root->SetSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
    m_panel->SetSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
    m_root->Update(Duration::FromSeconds(static_cast<f64>(m_deltaTime)));
    m_styles.ApplyTree(*m_root.Get());

    // Draw the tree into the VG batch.
    m_vg->Clear();
    {
        gui::DrawContext dc{ *m_vg };
        m_root->Draw(dc);
    }
    vg::VGBatch& batch = m_vg->GetBatch();

    // Present (same path as VGSandbox).
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != ErrorCode::Ok) return;

    m_renderer.BeginFrame(static_cast<i32>(m_frameIndex));
    const vg::renderer::VGRenderSlice slice = m_renderer.Prepare(batch, static_cast<i32>(m_frameIndex), m_width, m_height);

    m_pool->Reset();
    rhi::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != ErrorCode::Ok || enc == nullptr) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), rhi::ResourceState::Undefined, rhi::ResourceState::RenderTarget);

    rhi::ColorAttachment ca{};
    ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store;
    ca.clearValue = rhi::ClearColor(0.07f, 0.07f, 0.09f, 1.0f);
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

void GUISandbox::OnShutdown()
{
    if (m_device) m_device->WaitIdle();
    m_router.Reset();
    m_surface.Reset();
    m_bridge.Reset();
    m_root.Reset();
    m_counter.Reset();
    m_panel.Reset();
    m_renderer.Dispose();
    m_vg.Reset();
    m_fontService.Reset();
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool)  m_device->DestroyCommandPool(m_pool);
    if (m_fs)    m_device->DestroyShaderModule(m_fs);
    if (m_vs)    m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { GUISandbox app; return app.Run(argc, argv); }
