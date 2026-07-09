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

    // Appends "<n>" into buf (small values); caller supplies the prefix.
    inline void AppendNum(char8_t* buf, usize& pos, i32 n)
    {
        char8_t d[12]; usize dc = 0; i32 v = n < 0 ? -n : n;
        if (v == 0) d[dc++] = u8'0';
        while (v > 0) { d[dc++] = static_cast<char8_t>(u8'0' + v % 10); v /= 10; }
        if (n < 0) buf[pos++] = u8'-';
        for (usize k = 0; k < dc; ++k) buf[pos++] = d[dc - 1 - k];
    }

    // A tree row view: draws depth-indented text (TreeView overlays the expand arrows). No DRACONIC_
    // OBJECT (this sample TU imports modules only, not the reflection header) - the adapter recovers it
    // via static_cast since it created the view.
    class TreeItemView final : public draconic::ui::View
    {
    public:
        void Set(StringView text, i32 depth) { m_text = String(text); m_depth = depth; }
        void OnDraw(draconic::ui::UIDrawContext& ctx) override
        {
            if (m_text.Size() > 0 && ctx.FontService() != nullptr)
            {
                const f32 textX = static_cast<f32>(m_depth + 1) * m_indent;
                const Color color = ResolveStyleColor(draconic::ui::StyleProperty::TextColor, Color{ 220.0f / 255.0f, 220.0f / 255.0f, 230.0f / 255.0f, 1.0f });
                if (fonts::CachedFont* font = ctx.FontService()->GetFont(ResolveStyleFontFamily(), 14.0f))
                {
                    ctx.VG().DrawText(m_text, font, Rectangle{ textX, 0, Width() - textX, Height() }, fonts::TextAlignment::Left, fonts::VerticalAlignment::Middle, color);
                }
            }
        }
    private:
        String m_text;
        i32 m_depth = 0;
        f32 m_indent = 20.0f;
    };

    // Demo list adapter: N "Item k" labels.
    class DemoListAdapter final : public draconic::ui::ListAdapterBase
    {
    public:
        explicit DemoListAdapter(i32 count) : m_count(count) {}
        [[nodiscard]] i32 ItemCount() const override { return m_count; }
        [[nodiscard]] RefPtr<draconic::ui::View> CreateView(i32) override { return MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView{}); }
        void BindView(draconic::ui::View* view, i32 position) override
        {
            if (auto* label = draconic::core::Cast<draconic::ui::Label>(view))
            {
                char8_t buf[24] = u8"Item "; usize p = 5; AppendNum(buf, p, position + 1); buf[p] = 0;
                label->SetText(StringView(buf));
            }
        }
    private:
        i32 m_count;
    };

    // Demo tree adapter: 5 folders x 3 files; folder 0 has a subfolder with 2 files.
    class DemoTreeAdapter final : public draconic::ui::ITreeAdapter
    {
    public:
        [[nodiscard]] i32 RootCount() const override { return 5; }
        [[nodiscard]] i32 GetChildCount(i32 nodeId) const override
        {
            if (nodeId == -1) return 5;
            if (nodeId >= 0 && nodeId < 5) return (nodeId == 0) ? 4 : 3;
            if (nodeId == 50) return 2;
            return 0;
        }
        [[nodiscard]] i32 GetChildId(i32 parentId, i32 childIndex) const override
        {
            if (parentId == -1) return childIndex;
            if (parentId >= 0 && parentId < 5) { if (parentId == 0 && childIndex == 3) return 50; return 100 + parentId * 10 + childIndex; }
            if (parentId == 50) return 500 + childIndex;
            return -1;
        }
        [[nodiscard]] i32 GetDepth(i32 nodeId) const override { if (nodeId >= 500) return 2; if (nodeId >= 100 || nodeId == 50) return 1; return 0; }
        [[nodiscard]] bool HasChildren(i32 nodeId) const override { return (nodeId >= 0 && nodeId < 5) || nodeId == 50; }
        [[nodiscard]] RefPtr<draconic::ui::View> CreateView(i32) override { return MakeRef<TreeItemView>(DefaultAllocator()); }
        void BindView(draconic::ui::View* view, i32 nodeId, i32 depth, bool) override
        {
            auto* item = static_cast<TreeItemView*>(view); // adapter created it, so the type is known
            char8_t buf[24]; usize p = 0; const char8_t* pre = HasChildren(nodeId) ? u8"Folder " : u8"File ";
            for (usize k = 0; pre[k] != 0; ++k) buf[p++] = pre[k];
            AppendNum(buf, p, nodeId); buf[p] = 0;
            item->Set(StringView(buf), depth);
        }
    };

    class DragChip; // fwd

    // Custom drag payload carrying the source chip. No DRACONIC_OBJECT (sample TU) - the drop targets
    // guard on Format() == "demo/chip" and static_cast, since only chips produce that format.
    class ChipDragData final : public draconic::ui::DragData
    {
    public:
        DragChip* SourceChip;
        explicit ChipDragData(DragChip* source) : draconic::ui::DragData(u8"demo/chip"), SourceChip(source) {}
    };

    // A draggable coloured chip (ColorView + IDragSource).
    class DragChip final : public draconic::ui::ColorView, public draconic::ui::IDragSource
    {
    public:
        explicit DragChip(draconic::core::Color color) : draconic::ui::ColorView(color, 30.0f, 30.0f) {}
        [[nodiscard]] draconic::ui::IDragSource* AsDragSource() override { return this; }
        [[nodiscard]] RefPtr<draconic::ui::DragData> CreateDragData() override { return MakeRef<ChipDragData>(DefaultAllocator(), this); }
        [[nodiscard]] RefPtr<draconic::ui::View> CreateDragVisual(draconic::ui::DragData*) override
        {
            auto panel = MakeRef<draconic::ui::Panel>(DefaultAllocator());
            panel->Padding = draconic::ui::Thickness{ 6, 2 };
            panel->SetStyle(draconic::ui::StyleProperty::Background, RefPtr<draconic::ui::Drawable>(MakeRef<draconic::ui::ColorDrawable>(DefaultAllocator(), Color.Value())));
            panel->AddView(MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8"chip")).Get());
            return panel;
        }
        void OnDragStarted(draconic::ui::DragData*) override { Opacity = 0.4f; }
        void OnDragCompleted(draconic::ui::DragData*, draconic::ui::DragDropEffects, bool) override { Opacity = 1.0f; }
    };

    // A container that accepts chip drops and reorders by swapping colours (FlexLayout + IDropTarget).
    class ChipReorderContainer final : public draconic::ui::FlexLayout, public draconic::ui::IDropTarget
    {
    public:
        [[nodiscard]] draconic::ui::IDropTarget* AsDropTarget() override { return this; }
        [[nodiscard]] draconic::ui::DragDropEffects CanAcceptDrop(draconic::ui::DragData* data, f32, f32) override { return data->Format() == StringView(u8"demo/chip") ? draconic::ui::DragDropEffects::Move : draconic::ui::DragDropEffects::None; }
        void OnDragEnter(draconic::ui::DragData*, f32, f32) override {}
        void OnDragOver(draconic::ui::DragData*, f32, f32) override {}
        void OnDragLeave(draconic::ui::DragData*) override {}
        [[nodiscard]] draconic::ui::DragDropEffects OnDrop(draconic::ui::DragData* data, f32 localX, f32) override
        {
            if (data->Format() != StringView(u8"demo/chip")) { return draconic::ui::DragDropEffects::None; }
            DragChip* source = static_cast<ChipDragData*>(data)->SourceChip;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                draconic::ui::View* child = GetChildAt(i);
                if (localX >= child->Bounds.x && localX < child->Bounds.x + child->Width())
                {
                    DragChip* target = static_cast<DragChip*>(child);
                    if (target != source)
                    {
                        const draconic::core::Color tmp = source->Color.Value();
                        source->Color.SetValue(target->Color.Value());
                        target->Color.SetValue(tmp);
                    }
                    return draconic::ui::DragDropEffects::Move;
                }
            }
            return draconic::ui::DragDropEffects::None;
        }
    };

    // A drop box that recolours to the dropped chip's colour (View + IDropTarget).
    class ColorDropBox final : public draconic::ui::View, public draconic::ui::IDropTarget
    {
    public:
        [[nodiscard]] draconic::ui::IDropTarget* AsDropTarget() override { return this; }
        void OnDraw(draconic::ui::UIDrawContext& ctx) override
        {
            const Rectangle bounds{ 0, 0, Width(), Height() };
            ctx.VG().FillRoundedRect(bounds, 4.0f, m_bg);
            ctx.VG().StrokeRoundedRect(bounds, 4.0f, Color{ 70.0f / 255.0f, 75.0f / 255.0f, 85.0f / 255.0f, 1.0f }, 1.0f);
            if (ctx.FontService() != nullptr)
            {
                if (fonts::CachedFont* font = ctx.FontService()->GetFont(ResolveStyleFontFamily(), 12.0f))
                {
                    ctx.VG().DrawText(m_text.AsView(), font, bounds, fonts::TextAlignment::Center, fonts::VerticalAlignment::Middle, Color{ 220.0f / 255.0f, 225.0f / 255.0f, 235.0f / 255.0f, 1.0f });
                }
            }
        }
        [[nodiscard]] draconic::ui::DragDropEffects CanAcceptDrop(draconic::ui::DragData* data, f32, f32) override { return data->Format() == StringView(u8"demo/chip") ? draconic::ui::DragDropEffects::Copy : draconic::ui::DragDropEffects::None; }
        void OnDragEnter(draconic::ui::DragData*, f32, f32) override { m_text = String(u8"Release!"); Invalidate(); }
        void OnDragOver(draconic::ui::DragData*, f32, f32) override {}
        void OnDragLeave(draconic::ui::DragData*) override { m_text = String(u8"Drop here"); Invalidate(); }
        [[nodiscard]] draconic::ui::DragDropEffects OnDrop(draconic::ui::DragData* data, f32, f32) override
        {
            if (data->Format() == StringView(u8"demo/chip")) { m_bg = static_cast<ChipDragData*>(data)->SourceChip->Color.Value(); m_text = String(u8"Dropped!"); Invalidate(); }
            return draconic::ui::DragDropEffects::Copy;
        }
    protected:
        void OnMeasure(draconic::ui::BoxConstraints constraints) override { MeasuredSize = Float2{ constraints.ConstrainWidth(constraints.MaxWidth), constraints.ConstrainHeight(30) }; }
    private:
        String m_text = String(u8"Drop here");
        Color m_bg{ 50.0f / 255.0f, 55.0f / 255.0f, 65.0f / 255.0f, 1.0f };
    };

    // A bordered area that opens a nested ContextMenu on right-click.
    class ContextMenuDemoArea final : public draconic::ui::View
    {
    public:
        void OnDraw(draconic::ui::UIDrawContext& ctx) override
        {
            const Rectangle bounds{ 0, 0, Width(), Height() };
            const Color bg = ResolveStyleColor(draconic::ui::StyleProperty::BorderColor, Color{ 50.0f / 255.0f, 55.0f / 255.0f, 65.0f / 255.0f, 1.0f });
            ctx.VG().FillRoundedRect(bounds, 4.0f, draconic::ui::Palette::Darken(bg, 0.3f));
            ctx.VG().StrokeRoundedRect(bounds, 4.0f, bg, 1.0f);
            if (ctx.FontService() != nullptr)
            {
                if (fonts::CachedFont* font = ctx.FontService()->GetFont(ResolveStyleFontFamily(), 14.0f))
                {
                    ctx.VG().DrawText(u8"Right-click for context menu", font, bounds, fonts::TextAlignment::Center, fonts::VerticalAlignment::Middle, Color{ 180.0f / 255.0f, 185.0f / 255.0f, 200.0f / 255.0f, 1.0f });
                }
            }
        }
        void OnMouseDown(draconic::ui::MouseEventArgs& e) override
        {
            if (e.Button != draconic::ui::MouseButton::Right || Context == nullptr) { return; }
            auto menu = MakeRef<draconic::ui::ContextMenu>(DefaultAllocator());
            menu->AddItem(u8"Cut", []() {});
            menu->AddItem(u8"Copy", []() {});
            menu->AddItem(u8"Paste", []() {});
            menu->AddSeparator();
            draconic::ui::MenuItem* sub = menu->AddSubmenu(u8"More");
            auto* subMenu = draconic::core::Cast<draconic::ui::ContextMenu>(sub->Submenu.Get());
            subMenu->AddItem(u8"Select All", []() {});
            subMenu->AddItem(u8"Find", []() {});
            subMenu->AddSeparator();
            draconic::ui::MenuItem* nested = subMenu->AddSubmenu(u8"Even More");
            auto* nestedMenu = draconic::core::Cast<draconic::ui::ContextMenu>(nested->Submenu.Get());
            nestedMenu->AddItem(u8"Nested Item 1", []() {});
            nestedMenu->AddItem(u8"Nested Item 2", []() {});
            menu->AddSeparator();
            menu->AddItem(u8"Disabled Item", []() {}, false);

            const Float2 screenPos = LocalToScreen(Float2{ e.X, e.Y });
            menu->Show(Context, screenPos.x, screenPos.y);
            e.Handled = true;
        }
    protected:
        void OnMeasure(draconic::ui::BoxConstraints constraints) override
        {
            MeasuredSize = Float2{ constraints.ConstrainWidth(constraints.MaxWidth), constraints.ConstrainHeight(80) };
        }
    };

    // A Button whose tooltip is custom (multi-line) content, via ITooltipProvider.
    class RichTooltipButton final : public draconic::ui::Button, public draconic::ui::ITooltipProvider
    {
    public:
        explicit RichTooltipButton(StringView text) : draconic::ui::Button(text) { IsTooltipInteractive = true; }
        [[nodiscard]] draconic::ui::ITooltipProvider* AsTooltipProvider() override { return this; }
        [[nodiscard]] RefPtr<draconic::ui::View> CreateTooltipContent() override
        {
            auto layout = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            layout->Direction = draconic::ui::Orientation::Vertical; layout->Spacing = 4.0f;
            layout->AddView(MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8"Rich Tooltip")).Get());
            layout->AddView(MakeRef<draconic::ui::Separator>(DefaultAllocator()).Get());
            auto l1 = MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8"This tooltip has multiple lines,")); l1->AddClass(u8"label-dim");
            layout->AddView(l1.Get());
            auto l2 = MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8"a separator, and custom content.")); l2->AddClass(u8"label-dim");
            layout->AddView(l2.Get());
            auto colorRow = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            colorRow->Direction = draconic::ui::Orientation::Horizontal; colorRow->Spacing = 4.0f;
            colorRow->AddView(MakeRef<draconic::ui::ColorView>(DefaultAllocator(), Color{ 220.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, 16.0f, 16.0f).Get());
            colorRow->AddView(MakeRef<draconic::ui::ColorView>(DefaultAllocator(), Color{ 60.0f / 255.0f, 180.0f / 255.0f, 60.0f / 255.0f, 1.0f }, 16.0f, 16.0f).Get());
            colorRow->AddView(MakeRef<draconic::ui::ColorView>(DefaultAllocator(), Color{ 60.0f / 255.0f, 60.0f / 255.0f, 220.0f / 255.0f, 1.0f }, 16.0f, 16.0f).Get());
            layout->AddView(colorRow.Get());
            return layout;
        }
    };

    // Demo grid adapter: N coloured cells.
    class DemoGridAdapter final : public draconic::ui::ListAdapterBase
    {
    public:
        explicit DemoGridAdapter(i32 count) : m_count(count) {}
        [[nodiscard]] i32 ItemCount() const override { return m_count; }
        [[nodiscard]] RefPtr<draconic::ui::View> CreateView(i32) override { return MakeRef<draconic::ui::ColorView>(DefaultAllocator(), Color{ 100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f }, 0.0f, 0.0f); }
        void BindView(draconic::ui::View* view, i32 position) override
        {
            if (auto* cv = draconic::core::Cast<draconic::ui::ColorView>(view))
            {
                const f32 r = (60 + (position * 7) % 160) / 255.0f;
                const f32 g = (80 + (position * 13) % 140) / 255.0f;
                const f32 b = (100 + (position * 23) % 120) / 255.0f;
                cv->Color.SetValue(Color{ r, g, b, 1.0f });
            }
        }
    private:
        i32 m_count;
    };
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
    void BuildTextInputTab(ui::TabView* tabView);
    void BuildDataControlsTab(ui::TabView* tabView);
    void BuildOverlaysTab(ui::TabView* tabView);
    void BuildDragDropTab(ui::TabView* tabView);

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
    UniquePtr<DemoListAdapter> m_listAdapter;     // borrowed by the ListView (Data Controls tab)
    UniquePtr<DemoTreeAdapter> m_treeAdapter;
    UniquePtr<DemoGridAdapter> m_gridAdapter;

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
    BuildTextInputTab(tabView.Get());
    BuildDataControlsTab(tabView.Get());
    BuildOverlaysTab(tabView.Get());
    BuildDragDropTab(tabView.Get());
}

// === Tab 8: Drag & Drop (reorderable chips + a colour drop box) ===
void UISandbox::BuildDragDropTab(ui::TabView* tabView)
{
    using ui::SizeSpec;
    using ui::Unit;

    auto demo = VFlex(8.0f);
    demo->Padding = ui::Thickness{ 12, 8 };
    tabView->AddTab(u8"Drag & Drop", demo.Get());

    demo->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Drag chips to reorder, or drop onto the box")).Get());
    demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());

    auto row = HFlex(8.0f);

    auto chips = MakeRef<ChipReorderContainer>(DefaultAllocator());
    chips->Direction = ui::Orientation::Horizontal;
    chips->Spacing = 4.0f;
    const Color chipColors[5] = {
        Color{ 220.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f }, Color{ 60.0f / 255.0f, 180.0f / 255.0f, 60.0f / 255.0f, 1.0f },
        Color{ 60.0f / 255.0f, 100.0f / 255.0f, 220.0f / 255.0f, 1.0f }, Color{ 220.0f / 255.0f, 180.0f / 255.0f, 40.0f / 255.0f, 1.0f },
        Color{ 180.0f / 255.0f, 60.0f / 255.0f, 220.0f / 255.0f, 1.0f } };
    for (const Color& c : chipColors) { chips->AddView(MakeRef<DragChip>(DefaultAllocator(), c).Get(), LP(SizeSpec::Fixed(Unit::Px(30)), SizeSpec::Fixed(Unit::Px(30)))); }
    row->AddView(chips.Get());

    { auto p = Grow(1); p->Height = SizeSpec::Fixed(Unit::Px(30)); row->AddView(MakeRef<ColorDropBox>(DefaultAllocator()).Get(), p); }
    demo->AddView(row.Get());
}

// === Tab 7: Overlays (ComboBox / Dialog / ContextMenu / Tooltips) ===
void UISandbox::BuildOverlaysTab(ui::TabView* tabView)
{
    using ui::SizeSpec;
    using ui::Unit;

    auto scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
    scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
    tabView->AddTab(u8"Overlays", scroll.Get());

    auto demo = VFlex(8.0f);
    demo->Padding = ui::Thickness{ 12, 8 };
    scroll->AddView(demo.Get());
    auto section = [&](const char8_t* title) { demo->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(title)).Get()); demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get()); };
    auto spacer = [&] { demo->AddView(MakeRef<ui::Spacer>(DefaultAllocator(), 0.0f, 4.0f).Get()); };
    const auto w200 = [&] { return LP(SizeSpec::Fixed(Unit::Px(200)), SizeSpec::Wrap()); };

    // ComboBox.
    section(u8"ComboBox");
    { auto c = MakeRef<ui::ComboBox>(DefaultAllocator()); c->AddItem(u8"Option 1"); c->AddItem(u8"Option 2"); c->AddItem(u8"Option 3"); demo->AddView(c.Get(), w200()); }
    { auto c = MakeRef<ui::ComboBox>(DefaultAllocator()); c->AddItem(u8"Red"); c->AddItem(u8"Green"); c->AddItem(u8"Blue"); c->SetSelectedIndex(1); demo->AddView(c.Get(), w200()); }

    // Dialog.
    spacer(); section(u8"Dialog");
    {
        auto row = HFlex(8.0f);
        ui::UIContext* ctx = &m_ctx;
        auto alertBtn = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Alert"));
        alertBtn->OnClick.Add(ui::Event<void(ui::ButtonBase*)>::Handler{ [ctx](ui::ButtonBase*) { ui::Dialog::Alert(u8"Information", u8"This is an alert dialog.")->Show(ctx); } });
        row->AddView(alertBtn.Get());
        auto confirmBtn = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Confirm"));
        confirmBtn->OnClick.Add(ui::Event<void(ui::ButtonBase*)>::Handler{ [ctx](ui::ButtonBase*) { ui::Dialog::Confirm(u8"Confirm", u8"Are you sure you want to proceed?")->Show(ctx); } });
        row->AddView(confirmBtn.Get());
        demo->AddView(row.Get());
    }

    // ContextMenu.
    spacer(); section(u8"ContextMenu (right-click below)");
    demo->AddView(MakeRef<ContextMenuDemoArea>(DefaultAllocator()).Get(), LP(SizeSpec::Match(), SizeSpec::Fixed(Unit::Px(80))));

    // Tooltips.
    spacer(); section(u8"Tooltips (hover below)");
    {
        auto row = HFlex(8.0f);
        auto tt = [&](const char8_t* text, const char8_t* tip, ui::TooltipPlacement placement, bool interactive)
        {
            auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(text));
            b->TooltipText = String(tip);
            b->TooltipPlacement = placement;
            b->IsTooltipInteractive = interactive;
            row->AddView(b.Get());
        };
        tt(u8"Bottom tooltip", u8"This appears below",           ui::TooltipPlacement::Bottom, false);
        tt(u8"Top tooltip",    u8"This appears above",           ui::TooltipPlacement::Top,    false);
        tt(u8"Right tooltip",  u8"This appears on the right",    ui::TooltipPlacement::Right,  false);
        tt(u8"Interactive",    u8"This tooltip stays while you hover it", ui::TooltipPlacement::Bottom, true);
        row->AddView(MakeRef<RichTooltipButton>(DefaultAllocator(), StringView(u8"Rich content")).Get());
        demo->AddView(row.Get());
    }
}

// === Tab 6: Data Controls (virtualized ListView / TreeView / GridView + adapters) ===
void UISandbox::BuildDataControlsTab(ui::TabView* tabView)
{
    auto dataDemo = HFlex(8.0f);
    dataDemo->Padding = ui::Thickness{ 8 };
    tabView->AddTab(u8"Data Controls", dataDemo.Get());

    auto column = [&](const char8_t* title) { auto col = VFlex(4.0f); col->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(title)).Get()); dataDemo->AddView(col.Get(), Grow(1)); return col; };

    // ListView (1000 items).
    {
        auto col = column(u8"ListView (1000 items)");
        m_listAdapter = MakeUnique<DemoListAdapter>(DefaultAllocator(), 1000);
        auto listView = MakeRef<ui::ListView>(DefaultAllocator());
        listView->SetAdapter(m_listAdapter.Get());
        col->AddView(listView.Get(), Grow(1));
    }

    // TreeView (hierarchy).
    {
        auto col = column(u8"TreeView");
        m_treeAdapter = MakeUnique<DemoTreeAdapter>(DefaultAllocator());
        auto treeView = MakeRef<ui::TreeView>(DefaultAllocator());
        treeView->SetAdapter(m_treeAdapter.Get());
        col->AddView(treeView.Get(), Grow(1));
    }

    // GridView (200 coloured cells).
    {
        auto col = column(u8"GridView (200 cells)");
        m_gridAdapter = MakeUnique<DemoGridAdapter>(DefaultAllocator(), 200);
        auto gridView = MakeRef<ui::GridView>(DefaultAllocator());
        gridView->SetAdapter(m_gridAdapter.Get());
        col->AddView(gridView.Get(), Grow(1));
    }
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

// === Tab 5: Text Input (EditText / PasswordBox / NumericField / EditableLabel) ===
void UISandbox::BuildTextInputTab(ui::TabView* tabView)
{
    using ui::SizeSpec;
    using ui::Unit;

    auto scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
    scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
    tabView->AddTab(u8"Text Input", scroll.Get());

    auto demo = VFlex(8.0f);
    demo->Padding = ui::Thickness{ 12, 8 };
    scroll->AddView(demo.Get());

    const auto w300 = [&] { return LP(SizeSpec::Fixed(Unit::Px(300)), SizeSpec::Wrap()); };
    const auto w200 = [&] { return LP(SizeSpec::Fixed(Unit::Px(200)), SizeSpec::Wrap()); };
    auto section = [&](const char8_t* title)
    {
        demo->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(title)).Get());
        demo->AddView(MakeRef<ui::Separator>(DefaultAllocator()).Get());
    };

    // --- EditText ---
    section(u8"EditText");
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetText(u8"Editable text"); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetPlaceholder(u8"Enter name..."); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetText(u8"Read-only text"); e->IsReadOnly.SetValue(true); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->Multiline.SetValue(true); e->SetText(u8"Line 1\nLine 2\nLine 3"); demo->AddView(e.Get(), LP(SizeSpec::Fixed(Unit::Px(300)), SizeSpec::Fixed(Unit::Px(80)))); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->MaxLength.SetValue(10); e->SetPlaceholder(u8"Max 10 chars"); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetFilter(ui::InputFilter::Digits()); e->SetPlaceholder(u8"Digits only"); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetPrefix(StringView(u8"$")); e->SetText(u8"100"); demo->AddView(e.Get(), w300()); }
    { auto e = MakeRef<ui::EditText>(DefaultAllocator()); e->SetSuffix(StringView(u8"px")); e->SetText(u8"16"); demo->AddView(e.Get(), w300()); }

    // --- PasswordBox ---
    demo->AddView(MakeRef<ui::Spacer>(DefaultAllocator(), 0.0f, 4.0f).Get());
    section(u8"PasswordBox");
    { auto p = MakeRef<ui::PasswordBox>(DefaultAllocator()); p->SetPlaceholder(u8"Password"); demo->AddView(p.Get(), w300()); }
    { auto p = MakeRef<ui::PasswordBox>(DefaultAllocator()); p->PasswordChar.SetValue(U'●'); p->SetPlaceholder(u8"Custom mask"); demo->AddView(p.Get(), w300()); }

    // --- NumericField ---
    demo->AddView(MakeRef<ui::Spacer>(DefaultAllocator(), 0.0f, 4.0f).Get());
    section(u8"NumericField");
    { auto n = MakeRef<ui::NumericField>(DefaultAllocator()); n->SetMin(0); n->SetMax(100); n->SetValue(42); demo->AddView(n.Get(), w200()); }
    { auto n = MakeRef<ui::NumericField>(DefaultAllocator()); n->SetMin(0); n->SetMax(100); n->ShowSpinButtons.SetValue(false); n->SetValue(25); demo->AddView(n.Get(), w200()); }
    { auto n = MakeRef<ui::NumericField>(DefaultAllocator()); n->SetMin(-10); n->SetMax(10); n->SetStep(0.5); n->SetDecimalPlaces(1); n->SetValue(0); demo->AddView(n.Get(), w200()); }
    { auto n = MakeRef<ui::NumericField>(DefaultAllocator()); n->SetMin(0); n->SetMax(999); n->SetDecimalPlaces(0); n->SetValue(100); demo->AddView(n.Get(), w200()); }
    { auto n = MakeRef<ui::NumericField>(DefaultAllocator()); n->SetMin(0); n->SetMax(360); n->SetDecimalPlaces(1); n->SetSuffix(StringView(u8"°")); n->SetValue(90); demo->AddView(n.Get(), w200()); }

    // Vector3-style editor: 3 numeric fields with coloured axis prefix labels.
    demo->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Vector3 Editor")).Get());
    {
        auto vecRow = HFlex(4.0f);
        auto axisField = [&](const char8_t* axis, Color color, f64 val)
        {
            auto n = MakeRef<ui::NumericField>(DefaultAllocator());
            n->SetMin(-999); n->SetMax(999); n->SetStep(0.1); n->SetDecimalPlaces(2);
            n->ShowSpinButtons.SetValue(false); n->SetValue(val);
            auto prefix = MakeRef<ui::Label>(DefaultAllocator(), StringView(axis));
            prefix->TextColor.SetValue(Optional<Color>{ color });
            n->SetPrefix(prefix.Get());
            vecRow->AddView(n.Get(), Grow(1));
        };
        axisField(u8"X", Color{ 220.0f / 255.0f, 80.0f / 255.0f, 80.0f / 255.0f, 1.0f }, 1.06);
        axisField(u8"Y", Color{ 80.0f / 255.0f, 200.0f / 255.0f, 80.0f / 255.0f, 1.0f }, 0.0);
        axisField(u8"Z", Color{ 80.0f / 255.0f, 120.0f / 255.0f, 220.0f / 255.0f, 1.0f }, 2.17);
        demo->AddView(vecRow.Get(), LP(SizeSpec::Fixed(Unit::Px(400)), SizeSpec::Wrap()));
    }

    // --- EditableLabel ---
    demo->AddView(MakeRef<ui::Spacer>(DefaultAllocator(), 0.0f, 4.0f).Get());
    section(u8"EditableLabel (double-click to edit)");
    { auto el = MakeRef<ui::EditableLabel>(DefaultAllocator()); el->SetText(u8"Double-click me"); el->SlowClickToEdit.SetValue(false); demo->AddView(el.Get(), w300()); }
    { auto el = MakeRef<ui::EditableLabel>(DefaultAllocator()); el->SetText(u8"Slow-click me"); el->DoubleClickToEdit.SetValue(false); demo->AddView(el.Get(), w300()); }
    {
        auto el = MakeRef<ui::EditableLabel>(DefaultAllocator());
        el->SetText(u8"With validation");
        el->ValidateRename = Function<bool(StringView)>{ [](StringView text)
        {
            const StringView bad = u8"bad";
            if (text.Size() < bad.Size()) { return true; }
            for (usize i = 0; i + bad.Size() <= text.Size(); ++i)
            {
                if (StringView{ text.Data() + i, bad.Size() } == bad) { return false; }
            }
            return true;
        } };
        demo->AddView(el.Get(), w300());
    }
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
    m_listAdapter.Reset();
    m_treeAdapter.Reset();
    m_gridAdapter.Reset();
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
