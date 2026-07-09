// UI Sandbox - the first on-screen test of draconic.ui (the Sedulous.UI port). Builds a small View tree
// (a themed FlexLayout panel of controls), styles it with the ported DarkTheme StyleSheet, lays it out
// with UIContext, and renders it through the same VG -> VGRenderer -> RHI path as VGSandbox/GUISandbox.
// Input is driven by a fullscreen InputSurface (gated by an InputRouter) pumped into the UIContext's
// InputManager via UiInputBridge; keyboard/text go through the bridge's event path, and the text field's
// IME is driven from focus (WantsTextInput). A faithful-but-minimal analogue of Sedulous's UISandbox
// (sans the Toolkit), to prove the port end-to-end.

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
import draconic.ui;
import draconic.ui.shell;

using namespace draconic::core;
namespace sf = draconic::samples::framework;
namespace rhi = draconic::rhi;
namespace shaders = draconic::shaders;
namespace shell = draconic::shell;
namespace image = draconic::image;
namespace fonts = draconic::fonts;
namespace vg = draconic::vg;
namespace ui = draconic::ui;

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

#ifndef DRACONIC_UI_FONT_PATH
#define DRACONIC_UI_FONT_PATH ""
#endif

}

class UISandbox : public sf::SampleApp
{
public:
    UISandbox() { m_width = 820; m_height = 720; }
    StringView Title() const override { return u8"UI Sandbox (draconic.ui)"; }
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
        return !StringView(reinterpret_cast<const utf8char*>(DRACONIC_UI_FONT_PATH)).IsEmpty();
    }
    [[nodiscard]] static RefPtr<ui::FlexLayoutParams> LP(ui::SizeSpec w, ui::SizeSpec h)
    {
        auto p = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        p->Width = w; p->Height = h;
        return p;
    }
    [[nodiscard]] static RefPtr<ui::FlexLayoutParams> Grow(f32 g)
    {
        auto p = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        p->Grow = g;
        return p;
    }
    [[nodiscard]] static RefPtr<ui::FlexLayout> VFlex(f32 spacing = 0.0f)
    {
        auto f = MakeRef<ui::FlexLayout>(DefaultAllocator());
        f->Direction = ui::Orientation::Vertical; f->Spacing = spacing;
        return f;
    }
    [[nodiscard]] static RefPtr<ui::FlexLayout> HFlex(f32 spacing = 0.0f)
    {
        auto f = MakeRef<ui::FlexLayout>(DefaultAllocator());
        f->Direction = ui::Orientation::Horizontal; f->Spacing = spacing;
        return f;
    }
    [[nodiscard]] RefPtr<ui::Panel> MakeBox(Color color, StringView text);
    void BuildControlsTab(ui::TabView* tabView);
    void BuildScrollViewTab(ui::TabView* tabView);
    void BuildLayoutsTab(ui::TabView* tabView);
    void BuildTabPlacementTab(ui::TabView* tabView);

    // Render plumbing (mirrors VGSandbox/GUISandbox).
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

    // UI.
    ui::UIContext             m_ctx;
    RefPtr<ui::RootView>      m_root;
    RefPtr<ui::StyleSheet>    m_sheet;
    RefPtr<ui::FlexLayout>    m_main;
    UniquePtr<image::OwnedImageData> m_testImage; // borrowed by the ImageView/DrawableView demos
    RefPtr<ui::RepeatButton> m_repeatBtn;         // ticked each frame (hold-to-repeat)
    i32 m_repeatCount = 0;

    UniquePtr<ui::UiInputBridge>   m_bridge;
    UniquePtr<shell::InputSurface> m_surface;
    UniquePtr<shell::InputRouter>  m_router;
};

Status UISandbox::OnInit()
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
        const StringView fontPath(reinterpret_cast<const utf8char*>(DRACONIC_UI_FONT_PATH));
        LoadFontSize(fontPath, 14.0f);
        LoadFontSize(fontPath, 16.0f);
        LoadFontSize(fontPath, 24.0f);
    }

    m_vg = MakeUnique<vg::VGContext>(DefaultAllocator(), m_fontService.Get());

    BuildUI();
    return ErrorCode::Ok;
}

void UISandbox::LoadFontSize(StringView path, f32 pixelHeight)
{
    fonts::FontLoadOptions options = fonts::FontLoadOptions::ExtendedLatin();
    options.pixelHeight = pixelHeight;
    (void)m_fontService->LoadFont(u8"Roboto", path, options);
}

void UISandbox::BuildUI()
{
    m_ctx.SetFontService(m_fontService.Get());
    m_sheet = ui::DarkTheme::Create();
    m_ctx.SetStyleSheet(m_sheet);

    m_root = MakeRef<ui::RootView>(DefaultAllocator());
    m_root->ViewportSize = Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) };
    m_root->DpiScale = 1.0f;
    m_ctx.AddRootView(m_root.Get());

    // A 32x32 RGBA checker/gradient test image for the ImageView/DrawableView demos.
    {
        static u8 pixels[32 * 32 * 4];
        for (i32 y = 0; y < 32; ++y)
            for (i32 x = 0; x < 32; ++x)
            {
                const usize o = static_cast<usize>((y * 32 + x) * 4);
                const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                pixels[o + 0] = static_cast<u8>(checker ? 60 + x * 5 : 30);
                pixels[o + 1] = static_cast<u8>(checker ? 120 : 40 + y * 5);
                pixels[o + 2] = static_cast<u8>(checker ? 190 : 90);
                pixels[o + 3] = 255;
            }
        m_testImage = MakeUnique<image::OwnedImageData>(DefaultAllocator(), 32, 32,
            image::PixelFormat::RGBA8, Span<const u8>(pixels, sizeof(pixels)));
    }

    // Main vertical layout filling the window, with a TabView (mirrors Sedulous UISandbox).
    m_main = VFlex();
    m_root->AddView(m_main.Get());

    auto tabView = MakeRef<ui::TabView>(DefaultAllocator());
    tabView->TabsClosable.SetValue(false);
    m_main->AddView(tabView.Get(), Grow(1));

    BuildControlsTab(tabView.Get());
    BuildScrollViewTab(tabView.Get());
    BuildLayoutsTab(tabView.Get());
    BuildTabPlacementTab(tabView.Get());
}

// A themed labelled colour box (Sedulous UISandbox's MakeBox helper).
RefPtr<ui::Panel> UISandbox::MakeBox(Color color, StringView text)
{
    auto panel = MakeRef<ui::Panel>(DefaultAllocator());
    panel->SetStyle(ui::StyleProperty::Background, RefPtr<ui::Drawable>(MakeRef<ui::ColorDrawable>(DefaultAllocator(), color)));
    panel->Padding = ui::Thickness{ 8, 4, 8, 4 };
    auto label = MakeRef<ui::Label>(DefaultAllocator(), text);
    label->FontSize.SetValue(Optional<f32>{ 11.0f });
    label->HAlign.SetValue(fonts::TextAlignment::Center);
    label->VAlign.SetValue(fonts::VerticalAlignment::Middle);
    panel->AddView(label.Get());
    return panel;
}

// === Tab 2: ScrollView (overlay / reserved / horizontal) ===
void UISandbox::BuildScrollViewTab(ui::TabView* tabView)
{
    auto scrollDemo = HFlex(8.0f);
    scrollDemo->Padding = ui::Thickness{ 12, 8 };
    tabView->AddTab(u8"ScrollView", scrollDemo.Get());

    auto column = [&](const char8_t* title, ui::ScrollBarModeValue mode, ui::ScrollBarPolicy vPol, ui::ScrollBarPolicy hPol, bool horizontal, i32 count)
    {
        auto col = VFlex(4.0f);
        col->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(title)).Get());
        auto scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
        scroll->ScrollBarMode.SetValue(mode);
        scroll->VScrollBarPolicy.SetValue(vPol);
        scroll->HScrollBarPolicy.SetValue(hPol);
        auto content = horizontal ? HFlex(4.0f) : VFlex(4.0f);
        for (i32 i = 0; i < count; ++i)
        {
            if (horizontal)
            {
                content->AddView(MakeRef<ui::ColorView>(DefaultAllocator(),
                    Color{ (60 + i * 9) / 255.0f, (100 + i * 5) / 255.0f, (180 - i * 6) / 255.0f, 1.0f }, 60.0f, 60.0f).Get());
            }
            else
            {
                char8_t buf[24]; usize p = 0;
                const char8_t* pre = title;
                for (usize k = 0; pre[k] != 0 && k < 8; ++k) buf[p++] = pre[k];
                buf[p++] = u8' '; i32 v = i + 1; char8_t d[4]; usize dc = 0;
                if (v == 0) d[dc++] = u8'0'; while (v > 0) { d[dc++] = static_cast<char8_t>(u8'0' + v % 10); v /= 10; }
                for (usize k = 0; k < dc; ++k) buf[p++] = d[dc - 1 - k]; buf[p] = 0;
                content->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(buf)).Get());
            }
        }
        scroll->AddView(content.Get());
        col->AddView(scroll.Get(), Grow(1));
        scrollDemo->AddView(col.Get(), Grow(1));
    };

    column(u8"Overlay Mode",  ui::ScrollBarModeValue::Overlay,  ui::ScrollBarPolicy::Auto,   ui::ScrollBarPolicy::Auto,   false, 30);
    column(u8"Reserved Mode", ui::ScrollBarModeValue::Reserved, ui::ScrollBarPolicy::Auto,   ui::ScrollBarPolicy::Auto,   false, 30);
    column(u8"Horizontal",    ui::ScrollBarModeValue::Reserved, ui::ScrollBarPolicy::Always, ui::ScrollBarPolicy::Always, true,  20);
}

// === Tab 3: Layouts (Flex / Dock / Grid / Frame / Flow / Absolute) ===
void UISandbox::BuildLayoutsTab(ui::TabView* tabView)
{
    using ui::SizeSpec;
    using ui::Unit;

    auto layoutScroll = MakeRef<ui::ScrollView>(DefaultAllocator());
    layoutScroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
    tabView->AddTab(u8"Layouts", layoutScroll.Get());

    auto demo = VFlex(16.0f);
    demo->Padding = ui::Thickness{ 12 };
    {
        auto lp = MakeRef<ui::LayoutParams>(DefaultAllocator());
        lp->Width = SizeSpec::Match();
        layoutScroll->AddView(demo.Get(), lp);
    }

    auto dimLabel = [&](const char8_t* text)
    {
        auto l = MakeRef<ui::Label>(DefaultAllocator(), StringView(text));
        l->AddClass(u8"label-dim");
        l->FontSize.SetValue(Optional<f32>{ 12.0f });
        demo->AddView(l.Get());
    };

    // FlexLayout.
    dimLabel(u8"FlexLayout - rows and columns with grow/shrink");
    {
        auto flexH = HFlex(4.0f);
        flexH->AddView(MakeBox(Color{ 100.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"Fixed 80px").Get(), LP(SizeSpec::Fixed(Unit::Px(80)), SizeSpec::Fixed(Unit::Px(50))));
        flexH->AddView(MakeBox(Color{ 60.0f / 255.0f, 100.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"Grow 1").Get(), [&]{ auto p = Grow(1); p->Height = SizeSpec::Fixed(Unit::Px(50)); return p; }());
        flexH->AddView(MakeBox(Color{ 60.0f / 255.0f, 60.0f / 255.0f, 100.0f / 255.0f, 1.0f }, u8"Grow 2").Get(), [&]{ auto p = Grow(2); p->Height = SizeSpec::Fixed(Unit::Px(50)); return p; }());
        demo->AddView(flexH.Get(), LP(SizeSpec::Match(), SizeSpec::Wrap()));

        auto flexV = VFlex(4.0f);
        flexV->AddView(MakeBox(Color{ 90.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"Top").Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(30))));
        flexV->AddView(MakeBox(Color{ 50.0f / 255.0f, 90.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"Middle (grow)").Get(), [&]{ auto p = Grow(1); p->Width = SizeSpec::Match(); return p; }());
        flexV->AddView(MakeBox(Color{ 50.0f / 255.0f, 50.0f / 255.0f, 90.0f / 255.0f, 1.0f }, u8"Bottom").Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(30))));
        demo->AddView(flexV.Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(120))));
    }

    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // DockLayout.
    dimLabel(u8"DockLayout - dock children to edges, last fills remaining");
    {
        auto dock = MakeRef<ui::DockLayout>(DefaultAllocator());
        dock->LastChildFill = true;
        auto dockLp = [](ui::Dock d, SizeSpec w, SizeSpec h) { auto p = MakeRef<ui::DockLayoutParams>(DefaultAllocator(), d); p->Width = w; p->Height = h; return p; };
        dock->AddView(MakeBox(Color{ 100.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"Top").Get(),    dockLp(ui::Dock::Top,    SizeSpec::Wrap(), SizeSpec::Fixed(Unit::Px(30))));
        dock->AddView(MakeBox(Color{ 60.0f / 255.0f, 60.0f / 255.0f, 100.0f / 255.0f, 1.0f }, u8"Bottom").Get(), dockLp(ui::Dock::Bottom, SizeSpec::Wrap(), SizeSpec::Fixed(Unit::Px(30))));
        dock->AddView(MakeBox(Color{ 60.0f / 255.0f, 100.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"Left").Get(),   dockLp(ui::Dock::Left,   SizeSpec::Fixed(Unit::Px(60)), SizeSpec::Wrap()));
        dock->AddView(MakeBox(Color{ 100.0f / 255.0f, 100.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"Right").Get(), dockLp(ui::Dock::Right,  SizeSpec::Fixed(Unit::Px(60)), SizeSpec::Wrap()));
        dock->AddView(MakeBox(Color{ 70.0f / 255.0f, 70.0f / 255.0f, 70.0f / 255.0f, 1.0f }, u8"Fill").Get());
        demo->AddView(dock.Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(150))));
    }

    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // GridLayout.
    dimLabel(u8"GridLayout - rows and columns with flex/fixed sizing");
    {
        auto grid = MakeRef<ui::GridLayout>(DefaultAllocator());
        grid->Columns.PushBack(ui::TrackSize::Fixed(80));
        grid->Columns.PushBack(ui::TrackSize::Flex(1));
        grid->Columns.PushBack(ui::TrackSize::Flex(2));
        grid->Rows.PushBack(ui::TrackSize::Fixed(35));
        grid->Rows.PushBack(ui::TrackSize::Fixed(35));
        grid->Rows.PushBack(ui::TrackSize::Fixed(35));
        grid->ColumnSpacing = 4; grid->RowSpacing = 4;
        auto cell = [](i32 row, i32 col, i32 span) { auto p = MakeRef<ui::GridLayoutParams>(DefaultAllocator()); p->Row = row; p->Column = col; p->ColumnSpan = span; return p; };
        grid->AddView(MakeBox(Color{ 80.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"0,0").Get(), cell(0, 0, 1));
        grid->AddView(MakeBox(Color{ 50.0f / 255.0f, 80.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"0,1").Get(), cell(0, 1, 1));
        grid->AddView(MakeBox(Color{ 50.0f / 255.0f, 50.0f / 255.0f, 80.0f / 255.0f, 1.0f }, u8"0,2").Get(), cell(0, 2, 1));
        grid->AddView(MakeBox(Color{ 70.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f }, u8"1,0").Get(), cell(1, 0, 1));
        grid->AddView(MakeBox(Color{ 40.0f / 255.0f, 70.0f / 255.0f, 40.0f / 255.0f, 1.0f }, u8"Span 2 cols").Get(), cell(1, 1, 2));
        grid->AddView(MakeBox(Color{ 60.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 1.0f }, u8"Span 3 cols").Get(), cell(2, 0, 3));
        demo->AddView(grid.Get(), LP(SizeSpec::Match(), SizeSpec::Wrap()));
    }

    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // FrameLayout.
    dimLabel(u8"FrameLayout - overlapping children with gravity positioning");
    {
        auto frame = MakeRef<ui::FrameLayout>(DefaultAllocator());
        auto grav = [](ui::Gravity g) { auto p = MakeRef<ui::FrameLayoutParams>(DefaultAllocator()); p->Gravity = g; return p; };
        frame->AddView(MakeBox(Color{ 40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f }, u8"Background (Fill)").Get(), grav(ui::Gravity::Fill));
        frame->AddView(MakeBox(Color{ 100.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"TopLeft").Get(), grav(ui::Gravity::TopLeft));
        frame->AddView(MakeBox(Color{ 50.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"TopRight").Get(), grav(ui::Gravity::TopRight));
        frame->AddView(MakeBox(Color{ 50.0f / 255.0f, 50.0f / 255.0f, 100.0f / 255.0f, 1.0f }, u8"Center").Get(), grav(ui::Gravity::Center));
        frame->AddView(MakeBox(Color{ 100.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"BottomLeft").Get(), grav(ui::Gravity::BottomLeft));
        frame->AddView(MakeBox(Color{ 100.0f / 255.0f, 50.0f / 255.0f, 100.0f / 255.0f, 1.0f }, u8"BottomRight").Get(), grav(ui::Gravity::BottomRight));
        demo->AddView(frame.Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(140))));
    }

    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // FlowLayout.
    dimLabel(u8"FlowLayout - wraps children to next line when space runs out");
    {
        auto flow = MakeRef<ui::FlowLayout>(DefaultAllocator());
        flow->HSpacing = 4.0f; flow->VSpacing = 4.0f;
        const char8_t* tags[15] = { u8"Fire", u8"Water", u8"Earth", u8"Wind", u8"Electric", u8"Dark", u8"Light", u8"Neutral",
            u8"Poison", u8"Burn", u8"Stun", u8"Freeze", u8"Shield", u8"Heal", u8"Speed Up" };
        const Color tagColors[15] = {
            Color{ 140.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }, Color{ 50.0f / 255.0f, 80.0f / 255.0f, 140.0f / 255.0f, 1.0f },
            Color{ 60.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f }, Color{ 70.0f / 255.0f, 130.0f / 255.0f, 130.0f / 255.0f, 1.0f },
            Color{ 130.0f / 255.0f, 120.0f / 255.0f, 40.0f / 255.0f, 1.0f }, Color{ 80.0f / 255.0f, 50.0f / 255.0f, 100.0f / 255.0f, 1.0f },
            Color{ 130.0f / 255.0f, 120.0f / 255.0f, 80.0f / 255.0f, 1.0f }, Color{ 80.0f / 255.0f, 80.0f / 255.0f, 80.0f / 255.0f, 1.0f },
            Color{ 100.0f / 255.0f, 60.0f / 255.0f, 120.0f / 255.0f, 1.0f }, Color{ 140.0f / 255.0f, 70.0f / 255.0f, 30.0f / 255.0f, 1.0f },
            Color{ 120.0f / 255.0f, 100.0f / 255.0f, 30.0f / 255.0f, 1.0f }, Color{ 40.0f / 255.0f, 100.0f / 255.0f, 130.0f / 255.0f, 1.0f },
            Color{ 50.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f }, Color{ 50.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f },
            Color{ 30.0f / 255.0f, 100.0f / 255.0f, 130.0f / 255.0f, 1.0f } };
        for (i32 i = 0; i < 15; ++i) { flow->AddView(MakeBox(tagColors[i], StringView(tags[i])).Get()); }
        demo->AddView(flow.Get(), LP(SizeSpec::Match(), SizeSpec::Wrap()));
    }

    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // AbsoluteLayout.
    dimLabel(u8"AbsoluteLayout - explicit pixel positioning");
    {
        auto abs = MakeRef<ui::AbsoluteLayout>(DefaultAllocator());
        auto at = [](f32 x, f32 y) { auto p = MakeRef<ui::AbsoluteLayoutParams>(DefaultAllocator()); p->X = x; p->Y = y; return p; };
        abs->AddView(MakeBox(Color{ 60.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, u8"x:0 y:0").Get(), at(0, 0));
        abs->AddView(MakeBox(Color{ 100.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"x:100 y:10").Get(), at(100, 10));
        abs->AddView(MakeBox(Color{ 50.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f }, u8"x:50 y:60").Get(), at(50, 60));
        abs->AddView(MakeBox(Color{ 50.0f / 255.0f, 50.0f / 255.0f, 100.0f / 255.0f, 1.0f }, u8"x:200 y:40").Get(), at(200, 40));
        demo->AddView(abs.Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(110))));
    }
}

// === Tab 4: Tab Placement (nested TabViews in a 2x2 grid, closable) ===
void UISandbox::BuildTabPlacementTab(ui::TabView* tabView)
{
    auto demo = MakeRef<ui::GridLayout>(DefaultAllocator());
    demo->Columns.PushBack(ui::TrackSize::Flex(1));
    demo->Columns.PushBack(ui::TrackSize::Flex(1));
    demo->Rows.PushBack(ui::TrackSize::Flex(1));
    demo->Rows.PushBack(ui::TrackSize::Flex(1));
    demo->ColumnSpacing = 4; demo->RowSpacing = 4;
    tabView->AddTab(u8"Tab Placement", demo.Get(), true);

    auto cell = [](i32 row, i32 col) { auto p = MakeRef<ui::GridLayoutParams>(DefaultAllocator()); p->Row = row; p->Column = col; return p; };
    auto placed = [&](ui::TabPlacement placement, const char8_t* a, const char8_t* b, i32 row, i32 col)
    {
        auto tabs = MakeRef<ui::TabView>(DefaultAllocator());
        tabs->Placement.SetValue(placement);
        tabs->AddTab(StringView(a), MakeRef<ui::Label>(DefaultAllocator(), StringView(a)).Get());
        tabs->AddTab(StringView(b), MakeRef<ui::Label>(DefaultAllocator(), StringView(b)).Get());
        demo->AddView(tabs.Get(), cell(row, col));
    };
    placed(ui::TabPlacement::Top,    u8"Top A",    u8"Top B",    0, 0);
    placed(ui::TabPlacement::Bottom, u8"Bot A",    u8"Bot B",    0, 1);
    placed(ui::TabPlacement::Left,   u8"Left A",   u8"Left B",   1, 0);
    placed(ui::TabPlacement::Right,  u8"Right A",  u8"Right B",  1, 1);
}

// === Tab 1: Controls === (faithful port of Sedulous UISandbox's Controls tab)
void UISandbox::BuildControlsTab(ui::TabView* tabView)
{
    using ui::SizeSpec;
    using ui::Unit;

    auto body = HFlex(4.0f);
    tabView->AddTab(u8"Controls", body.Get());

    // --- Left panel: control showcase ---
    auto leftPanel = VFlex(8.0f);
    leftPanel->Padding = ui::Thickness{ 12, 8 };
    body->AddView(leftPanel.Get(), LP(SizeSpec::Fixed(Unit::Px(300)), SizeSpec::Wrap()));

    auto btnRow = HFlex(6.0f);
    btnRow->AddView(MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Click Me")).Get());
    {
        auto disabled = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Disabled"));
        disabled->IsEnabled = false;
        btnRow->AddView(disabled.Get());
    }
    btnRow->AddView(MakeRef<ui::ToggleButton>(DefaultAllocator(), StringView(u8"Toggle")).Get());
    leftPanel->AddView(btnRow.Get());

    // ContentButton (icon + text, and a two-line variant).
    auto contentBtnRow = HFlex(6.0f);
    {
        auto iconText = HFlex(6.0f); iconText->AlignItems = ui::Align::Center;
        iconText->AddView(MakeRef<ui::ColorView>(DefaultAllocator(), Color{ 80.0f / 255.0f, 180.0f / 255.0f, 80.0f / 255.0f, 1.0f }, 12.0f, 12.0f).Get());
        iconText->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Icon + Text")).Get());
        contentBtnRow->AddView(MakeRef<ui::ContentButton>(DefaultAllocator(), iconText).Get());

        auto multi = VFlex(2.0f); multi->AlignItems = ui::Align::Center;
        auto l1 = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Line 1")); l1->FontSize.SetValue(Optional<f32>{ 12.0f });
        multi->AddView(l1.Get());
        auto l2 = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Line 2")); l2->FontSize.SetValue(Optional<f32>{ 10.0f });
        multi->AddView(l2.Get());
        contentBtnRow->AddView(MakeRef<ui::ContentButton>(DefaultAllocator(), multi).Get());
    }
    leftPanel->AddView(contentBtnRow.Get());

    // RepeatButton + a live count label.
    {
        auto repeatRow = HFlex(6.0f);
        auto repeatLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Count: 0"));
        auto repeatBtn = MakeRef<ui::RepeatButton>(DefaultAllocator(), StringView(u8"Hold Me"));
        ui::Label* lbl = repeatLabel.Get();
        i32* count = &m_repeatCount;
        repeatBtn->OnClick.Add(ui::Event<void(ui::ButtonBase*)>::Handler{ [lbl, count](ui::ButtonBase*)
        {
            ++(*count);
            char8_t buf[24] = u8"Count: "; usize p = 7; i32 v = *count; char8_t d[8]; usize dc = 0;
            if (v == 0) d[dc++] = u8'0'; while (v > 0) { d[dc++] = static_cast<char8_t>(u8'0' + v % 10); v /= 10; }
            for (usize k = 0; k < dc; ++k) buf[p++] = d[dc - 1 - k]; buf[p] = 0;
            lbl->SetText(StringView(buf));
        } });
        repeatRow->AddView(repeatBtn.Get());
        repeatRow->AddView(repeatLabel.Get());
        leftPanel->AddView(repeatRow.Get());
        m_repeatBtn = repeatBtn; // ticked each frame in OnRender (hold-to-repeat)
    }

    leftPanel->AddView(MakeRef<ui::Spacer>(DefaultAllocator(), 0.0f, 4.0f).Get());

    // Toggle controls.
    leftPanel->AddView(MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8"Enable sounds"), true).Get());
    leftPanel->AddView(MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8"Fullscreen")).Get());
    leftPanel->AddView(MakeRef<ui::ToggleSwitch>(DefaultAllocator(), StringView(u8"VSync")).Get());

    leftPanel->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // Radio group.
    {
        auto radioGroup = MakeRef<ui::RadioGroup>(DefaultAllocator());
        radioGroup->AddRadioButton(MakeRef<ui::RadioButton>(DefaultAllocator(), StringView(u8"Low")).Get());
        radioGroup->AddRadioButton(MakeRef<ui::RadioButton>(DefaultAllocator(), StringView(u8"Medium")).Get());
        radioGroup->AddRadioButton(MakeRef<ui::RadioButton>(DefaultAllocator(), StringView(u8"High")).Get());
        radioGroup->CheckAt(1);
        leftPanel->AddView(radioGroup.Get());
    }

    leftPanel->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // Slider + progress bar.
    leftPanel->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Volume")).Get());
    leftPanel->AddView(MakeRef<ui::Slider>(DefaultAllocator(), 0.0f, 100.0f, 75.0f).Get());
    leftPanel->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Loading...")).Get());
    {
        auto progressBar = MakeRef<ui::ProgressBar>(DefaultAllocator());
        progressBar->Value.SetValue(0.65f);
        leftPanel->AddView(progressBar.Get());
    }

    // --- Center panel: themed Panel with Expanders ---
    auto centerPanel = VFlex(8.0f);
    centerPanel->Padding = ui::Thickness{ 8 };
    body->AddView(centerPanel.Get(), Grow(1));

    {
        auto settingsPanel = MakeRef<ui::Panel>(DefaultAllocator());
        settingsPanel->Padding = ui::Thickness{ 8 };
        settingsPanel->AddClass(u8"panel");
        auto settingsLayout = VFlex(4.0f);
        settingsPanel->AddView(settingsLayout.Get());
        centerPanel->AddView(settingsPanel.Get());

        auto expander1 = MakeRef<ui::Expander>(DefaultAllocator(), StringView(u8"Graphics Settings"));
        auto content1 = VFlex(4.0f);
        content1->AddView(MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8"Anti-Aliasing")).Get());
        content1->AddView(MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8"Shadows"), true).Get());
        content1->AddView(MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8"Bloom"), true).Get());
        expander1->SetContent(content1.Get());
        settingsLayout->AddView(expander1.Get());

        auto expander2 = MakeRef<ui::Expander>(DefaultAllocator(), StringView(u8"Audio Settings"));
        auto content2 = VFlex(4.0f);
        content2->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Master Volume")).Get());
        content2->AddView(MakeRef<ui::Slider>(DefaultAllocator(), 0.0f, 100.0f, 80.0f).Get());
        content2->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Music Volume")).Get());
        content2->AddView(MakeRef<ui::Slider>(DefaultAllocator(), 0.0f, 100.0f, 50.0f).Get());
        expander2->SetContent(content2.Get());
        settingsLayout->AddView(expander2.Get());
    }

    // --- Right panel: ImageView scale modes + color swatches + SVG drawables ---
    auto rightPanel = VFlex(4.0f);
    rightPanel->Padding = ui::Thickness{ 4 };
    body->AddView(rightPanel.Get(), LP(SizeSpec::Fixed(Unit::Px(200)), SizeSpec::Wrap()));

    const image::ImageData* img = m_testImage.Get();
    auto addImage = [&](const char8_t* label, ui::ScaleType scale, bool clip, Optional<Color> tint)
    {
        rightPanel->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(label)).Get());
        auto iv = MakeRef<ui::ImageView>(DefaultAllocator(), img);
        iv->ScaleType.SetValue(scale);
        iv->ClipsContent = clip;
        if (tint.HasValue()) { iv->Tint.SetValue(tint.Value()); }
        rightPanel->AddView(iv.Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(48))));
    };
    addImage(u8"None",       ui::ScaleType::None,       true,  Optional<Color>{});
    addImage(u8"FitCenter",  ui::ScaleType::FitCenter,  false, Optional<Color>{});
    addImage(u8"FillBounds", ui::ScaleType::FillBounds, false, Optional<Color>{});
    addImage(u8"CenterCrop", ui::ScaleType::CenterCrop, false, Optional<Color>{});
    addImage(u8"Tinted",     ui::ScaleType::FitCenter,  false, Optional<Color>{ Color{ 1.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f } });

    rightPanel->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // Color swatches (FlowLayout).
    rightPanel->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"ColorView")).Get());
    {
        auto swatchFlow = MakeRef<ui::FlowLayout>(DefaultAllocator());
        swatchFlow->HSpacing = 4.0f; swatchFlow->VSpacing = 4.0f;
        const Color swatches[8] = {
            Color{ 220.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, Color{ 60.0f / 255.0f, 180.0f / 255.0f, 60.0f / 255.0f, 1.0f },
            Color{ 60.0f / 255.0f, 60.0f / 255.0f, 220.0f / 255.0f, 1.0f }, Color{ 220.0f / 255.0f, 180.0f / 255.0f, 40.0f / 255.0f, 1.0f },
            Color{ 180.0f / 255.0f, 60.0f / 255.0f, 180.0f / 255.0f, 1.0f }, Color{ 60.0f / 255.0f, 180.0f / 255.0f, 180.0f / 255.0f, 1.0f },
            Color{ 220.0f / 255.0f, 120.0f / 255.0f, 60.0f / 255.0f, 1.0f }, Color{ 120.0f / 255.0f, 60.0f / 255.0f, 220.0f / 255.0f, 1.0f } };
        for (const Color& c : swatches) { swatchFlow->AddView(MakeRef<ui::ColorView>(DefaultAllocator(), c, 40.0f, 40.0f).Get()); }
        rightPanel->AddView(swatchFlow.Get());
    }

    rightPanel->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    // DrawableView + SVG drawables.
    rightPanel->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"DrawableView + SVG")).Get());
    {
        auto svgRow = MakeRef<ui::FlowLayout>(DefaultAllocator());
        svgRow->HSpacing = 6.0f; svgRow->VSpacing = 6.0f;
        auto addSvg = [&](StringView svg, f32 sz, Optional<Color> tint)
        {
            RefPtr<ui::SVGDrawable> d = tint.HasValue() ? ui::SVGDrawable::FromString(svg, tint.Value()) : ui::SVGDrawable::FromString(svg);
            if (d) { svgRow->AddView(MakeRef<ui::DrawableView>(DefaultAllocator(), RefPtr<ui::Drawable>(d), sz, sz).Get()); }
        };
        addSvg(u8"<svg viewBox=\"0 0 48 48\"><circle cx=\"24\" cy=\"24\" r=\"22\" fill=\"#2A6BC0\" stroke=\"#1A4A90\" stroke-width=\"2\"/><text x=\"24\" y=\"30\" text-anchor=\"middle\" font-size=\"18\" font-weight=\"bold\" fill=\"#FFFFFF\">UI</text></svg>", 40.0f, Optional<Color>{});
        addSvg(u8"<svg viewBox=\"0 0 24 24\"><path d=\"M12 2L15.09 8.26L22 9.27L17 14.14L18.18 21.02L12 17.77L5.82 21.02L7 14.14L2 9.27L8.91 8.26L12 2Z\" fill=\"#FFD700\" stroke=\"#B8960F\" stroke-width=\"0.8\"/></svg>", 32.0f, Optional<Color>{});
        addSvg(u8"<svg viewBox=\"0 0 24 24\"><path d=\"M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z\" fill=\"#FF4444\"/></svg>", 32.0f, Optional<Color>{});
        addSvg(u8"<svg viewBox=\"0 0 24 24\"><path d=\"M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z\" fill=\"#FF4444\"/></svg>", 32.0f, Optional<Color>{ Color{ 100.0f / 255.0f, 200.0f / 255.0f, 1.0f, 1.0f } });
        rightPanel->AddView(svgRow.Get());
    }
}

void UISandbox::OnRender()
{
    // Input: a fullscreen InputSurface, gated by a router, pumped into the InputManager.
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
            m_bridge = MakeUnique<ui::UiInputBridge>(DefaultAllocator(), &m_ctx);
            m_bridge->SetTextInputTarget(m_window); // IME follows UI focus
        }
        m_surface->SetRegion(region);
        m_surface->SetContentSize(Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) });
        m_router->Update();
        m_bridge->PumpFromSurface(*m_surface);

        // Keyboard/text: dispatch the raw key/text events (mouse already handled by the pump).
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

    // Hold-to-repeat: tick the RepeatButton each frame (mirrors Sedulous UISandbox).
    if (m_repeatBtn) { m_repeatBtn->UpdateRepeat(m_deltaTime); }

    // Advance, lay out, and draw the UI tree.
    m_root->ViewportSize = Float2{ static_cast<f32>(m_width), static_cast<f32>(m_height) };
    m_ctx.BeginFrame(m_deltaTime);
    m_ctx.UpdateRootView(m_root.Get());

    m_vg->Clear();
    m_ctx.DrawRootView(m_root.Get(), *m_vg);
    vg::VGBatch& batch = m_vg->GetBatch();

    // Present (same path as VGSandbox/GUISandbox).
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

void UISandbox::OnShutdown()
{
    if (m_device) m_device->WaitIdle();
    m_router.Reset();
    m_surface.Reset();
    m_bridge.Reset();
    if (m_root) m_ctx.RemoveRootView(m_root.Get());
    m_repeatBtn.Reset();
    m_main.Reset();
    m_root.Reset();
    m_sheet.Reset();
    m_testImage.Reset();
    m_renderer.Dispose();
    m_vg.Reset();
    m_fontService.Reset();
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool)  m_device->DestroyCommandPool(m_pool);
    if (m_fs)    m_device->DestroyShaderModule(m_fs);
    if (m_vs)    m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { UISandbox app; return app.Run(argc, argv); }
