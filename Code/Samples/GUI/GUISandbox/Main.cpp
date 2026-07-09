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
            background-color: #3a6ea5;    /* base: blue */
            padding: 10;
            opacity: 1;
            transition: opacity 0.18s;
        }
        button:hover  { opacity: 0.9; }
        button:active { opacity: 0.78; }
        .danger { background-color: #b5453f; } /* class beats tag -> red */
        .accent { background-color: #3fa06a; } /*                  -> green */
    )";

    inline Color Col(f32 r, f32 g, f32 b, f32 a = 1.0f) { return Color{ r, g, b, a }; }

    // "Clicks: N" -> label (small non-negative values).
    void SetCounterText(gui::Label* label, i32 n)
    {
        char8_t buf[32] = u8"Clicks: ";
        usize pos = 8;
        char8_t digits[12]; usize dc = 0;
        i32 v = n < 0 ? -n : n;
        if (v == 0) digits[dc++] = u8'0';
        while (v > 0) { digits[dc++] = static_cast<char8_t>(u8'0' + (v % 10)); v /= 10; }
        if (n < 0) buf[pos++] = u8'-';
        for (usize k = 0; k < dc; ++k) buf[pos++] = digits[dc - 1 - k];
        buf[pos] = 0;
        label->SetText(StringView(buf));
    }

    // "N%" (0..1 -> 0..100) -> label.
    void SetPercentText(gui::Label* label, f32 value01)
    {
        const i32 pct = static_cast<i32>(value01 * 100.0f + 0.5f);
        char8_t buf[8]; usize pos = 0;
        char8_t digits[4]; usize dc = 0;
        i32 v = pct;
        if (v == 0) digits[dc++] = u8'0';
        while (v > 0) { digits[dc++] = static_cast<char8_t>(u8'0' + (v % 10)); v /= 10; }
        for (usize k = 0; k < dc; ++k) buf[pos++] = digits[dc - 1 - k];
        buf[pos++] = u8'%';
        buf[pos] = 0;
        label->SetText(StringView(buf));
    }
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
    RefPtr<gui::TextField>    m_textField;
    RefPtr<gui::Label>        m_echo;
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

    // Three buttons that are genuinely different: distinct style classes (so CSS gives them
    // distinct colors - class beats tag) and distinct behaviors.
    gui::Label* counter = m_counter.Get();
    i32* clicks = &m_clicks;

    auto makeButton = [&](const char8_t* label, const char8_t* cssClass, Function<void()> onClick)
    {
        auto btn = MakeRef<gui::Button>(DefaultAllocator());
        btn->SetSize(Float2{ 170.0f, 46.0f });
        btn->SetText(StringView(label));
        btn->SetFont(m_font);
        btn->SetTextColor(Col(1.0f, 1.0f, 1.0f));
        if (cssClass[0] != 0) btn->AddClass(StringView(cssClass));
        btn->SetOnClick(Move(onClick));
        row->AddChild(btn.Get());
    };

    makeButton(u8"Add +1", u8"",       [counter, clicks]() { ++(*clicks); SetCounterText(counter, *clicks); });
    makeButton(u8"Reset",  u8"danger", [counter, clicks]() { *clicks = 0; SetCounterText(counter, 0); });
    makeButton(u8"Add +5", u8"accent", [counter, clicks]() { *clicks += 5; SetCounterText(counter, *clicks); });

    // A checkbox + label row: toggling it highlights the counter.
    auto checkRow = MakeRef<gui::LinearLayout>(DefaultAllocator());
    checkRow->SetOrientation(gui::Orientation::Horizontal);
    checkRow->SetSize(Float2{ 360.0f, 30.0f });
    checkRow->SetSpacing(10.0f);
    m_panel->AddChild(checkRow.Get());

    auto check = MakeRef<gui::CheckBox>(DefaultAllocator());
    check->SetSize(Float2{ 22.0f, 22.0f });
    check->SetOnCheckedChanged([counter](bool on)
    {
        counter->SetTextColor(on ? Color{ 0.42f, 0.85f, 0.52f, 1.0f } : Color{ 0.75f, 0.82f, 0.9f, 1.0f });
    });
    checkRow->AddChild(check.Get());

    auto checkLabel = MakeRef<gui::Label>(DefaultAllocator());
    checkLabel->SetSize(Float2{ 220.0f, 26.0f });
    checkLabel->SetText(u8"Highlight counter");
    checkLabel->SetFont(m_font);
    checkLabel->SetTextColor(Col(0.8f, 0.85f, 0.9f));
    checkRow->AddChild(checkLabel.Get());

    // A slider + live percent label.
    auto sliderRow = MakeRef<gui::LinearLayout>(DefaultAllocator());
    sliderRow->SetOrientation(gui::Orientation::Horizontal);
    sliderRow->SetSize(Float2{ 380.0f, 30.0f });
    sliderRow->SetSpacing(12.0f);
    m_panel->AddChild(sliderRow.Get());

    auto slider = MakeRef<gui::Slider>(DefaultAllocator());
    slider->SetSize(Float2{ 220.0f, 24.0f });
    sliderRow->AddChild(slider.Get());

    auto sliderLabel = MakeRef<gui::Label>(DefaultAllocator());
    sliderLabel->SetSize(Float2{ 80.0f, 26.0f });
    sliderLabel->SetFont(m_font);
    sliderLabel->SetTextColor(Col(0.8f, 0.85f, 0.9f));
    sliderRow->AddChild(sliderLabel.Get());

    // A progress bar driven by the slider (so they move together).
    auto bar = MakeRef<gui::ProgressBar>(DefaultAllocator());
    bar->SetSize(Float2{ 340.0f, 14.0f });
    m_panel->AddChild(bar.Get());

    gui::Label* pct = sliderLabel.Get();
    gui::ProgressBar* barPtr = bar.Get();
    slider->SetOnValueChanged([pct, barPtr](f32 v) { SetPercentText(pct, v); barPtr->SetProgress(v); });
    slider->SetValue(0.5f); // fires the callback -> label "50%", bar half-filled

    // An editable text field (the keyboard/text path) + a live echo label.
    auto fieldRow = MakeRef<gui::LinearLayout>(DefaultAllocator());
    fieldRow->SetOrientation(gui::Orientation::Horizontal);
    fieldRow->SetSize(Float2{ 560.0f, 40.0f });
    fieldRow->SetSpacing(12.0f);
    m_panel->AddChild(fieldRow.Get());

    auto fieldPrompt = MakeRef<gui::Label>(DefaultAllocator());
    fieldPrompt->SetSize(Float2{ 60.0f, 34.0f });
    fieldPrompt->SetText(u8"Name:");
    fieldPrompt->SetFont(m_font);
    fieldPrompt->SetTextColor(Col(0.8f, 0.85f, 0.9f));
    fieldRow->AddChild(fieldPrompt.Get());

    m_textField = MakeRef<gui::TextField>(DefaultAllocator());
    m_textField->SetSize(Float2{ 300.0f, 34.0f });
    m_textField->SetPadding(gui::Thickness{ 8.0f, 6.0f, 8.0f, 6.0f });
    m_textField->SetFont(m_font);
    m_textField->SetTextColor(Col(0.96f, 0.97f, 0.99f));
    m_textField->SetBackground(MakeRef<gui::RectangleDrawable>(DefaultAllocator(), Col(0.20f, 0.22f, 0.27f)));
    m_textField->SetText(u8"click, then type");
    fieldRow->AddChild(m_textField.Get());

    m_echo = MakeRef<gui::Label>(DefaultAllocator());
    m_echo->SetSize(Float2{ 500.0f, 26.0f });
    m_echo->SetFont(m_font);
    m_echo->SetTextColor(Col(0.62f, 0.72f, 0.82f));
    m_echo->SetText(u8"echo: click, then type");
    m_panel->AddChild(m_echo.Get());

    gui::Label* echo = m_echo.Get();
    m_textField->SetOnTextChanged([echo](StringView v)
    {
        char8_t buf[64] = u8"echo: ";
        usize pos = 6;
        for (usize i = 0; i < v.Size() && pos < 62; ++i) buf[pos++] = v[i];
        buf[pos] = 0;
        echo->SetText(StringView(buf));
    });

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
            m_bridge->SetTextInputTarget(m_window); // the bridge drives IME on/off from focus
        }
        m_surface->SetRegion(region);
        m_surface->SetContentSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
        m_router->Update();
        m_bridge->PumpFromSurface(*m_surface);

        // Keyboard/text is NOT part of the mouse-only surface pump: dispatch the raw event
        // stream's key/text events through the bridge's event path (mouse events are skipped
        // here, since PumpFromSurface already handled them). Text-input enable/disable is
        // handled generically by the bridge (SetTextInputTarget below) from the focused
        // widget's WantsTextInput() - no per-widget wiring here.
        for (const shell::InputEvent& ev : input.Events())
        {
            switch (ev.kind)
            {
            case shell::InputEventKind::KeyDown:
            case shell::InputEventKind::KeyUp:
            case shell::InputEventKind::TextInput:
                m_bridge->Dispatch(ev);
                break;
            default:
                break;
            }
        }
    }

    // Blink the text field's caret while it holds focus.
    if (m_textField) m_textField->Update(static_cast<f64>(m_deltaTime));

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
    m_textField.Reset();
    m_echo.Reset();
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
