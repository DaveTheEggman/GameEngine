// Draconic GUI - theme + resource-backed CSS props: text color, font-family/size and
// background-image resolve through the StyleManager (with an IResourceProvider), and the
// built-in default theme restyles the standard widgets.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.fonts;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }

    // 6px advance / byte mock font (as in the Text tests).
    class MockFont : public fonts::IFont
    {
    public:
        core::StringView FamilyName() const override { return core::StringView(u8"mock"); }
        fonts::FontMetrics Metrics() const override
        {
            fonts::FontMetrics m; m.ascent = 10.0f; m.descent = -2.0f; m.lineGap = 0.0f;
            m.lineHeight = 12.0f; m.pixelHeight = 10.0f; m.scale = 1.0f; return m;
        }
        core::f32 PixelHeight() const override { return 10.0f; }
        fonts::GlyphInfo GetGlyphInfo(core::i32 cp) const override { fonts::GlyphInfo g; g.codepoint = cp; g.advanceWidth = 6.0f; return g; }
        core::f32 GetKerning(core::i32, core::i32) const override { return 0.0f; }
        bool HasGlyph(core::i32) const override { return true; }
        core::f32 MeasureString(core::StringView t) const override { return static_cast<core::f32>(t.Size()) * 6.0f; }
        core::f32 MeasureString(core::StringView t, core::Array<fonts::GlyphPosition>& o) const override { (void)o; return static_cast<core::f32>(t.Size()) * 6.0f; }
    };

    // A resource provider that hands out one drawable and one font, recording what was asked.
    class MockResources : public IResourceProvider
    {
    public:
        core::RefPtr<RectangleDrawable> drawable = core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), core::Color{ 0.9f, 0.1f, 0.1f, 1.0f });
        fonts::CachedFont font{ core::DefaultAllocator().New<MockFont>(), nullptr, nullptr };
        core::String lastDrawable, lastFamily;
        core::f32 lastSize = 0.0f;

        Drawable* GetDrawable(core::StringView name) override { lastDrawable = core::String(name); return drawable.Get(); }
        fonts::CachedFont* GetFont(core::StringView family, core::f32 size) override
        {
            lastFamily = core::String(family); lastSize = size; return &font;
        }
    };

    core::RefPtr<Label> ApplyCSS(const char8_t* css, IResourceProvider* res = nullptr)
    {
        auto label = core::MakeRef<Label>(core::DefaultAllocator());
        StyleManager mgr;
        mgr.SetStyleSheet(CSSParser::Parse(core::StringView(css)));
        if (res) mgr.SetResourceProvider(res);
        mgr.ApplyTo(*label.Get());
        return label;
    }
}

TEST_CASE("theme: color styles a label's text color")
{
    auto label = ApplyCSS(u8"label { color: #ff8800; }");
    CHECK(label->GetTextColor().r == doctest::Approx(1.0f));
    CHECK(label->GetTextColor().g == doctest::Approx(0x88 / 255.0f));
    CHECK(label->GetTextColor().b == doctest::Approx(0.0f));
}

TEST_CASE("theme: background-image resolves through the resource provider")
{
    MockResources res;
    auto label = core::MakeRef<Label>(core::DefaultAllocator());
    StyleManager mgr;
    mgr.SetStyleSheet(CSSParser::Parse(core::StringView(u8"label { background-image: url(panel); }")));
    mgr.SetResourceProvider(&res);
    mgr.ApplyTo(*label.Get());

    CHECK(res.lastDrawable.AsView() == core::StringView(u8"panel"));
    CHECK(label->GetBackground() == res.drawable.Get()); // the provided drawable is now the bg
}

TEST_CASE("theme: font-family + font-size resolve a font via the provider")
{
    MockResources res;
    auto label = core::MakeRef<Label>(core::DefaultAllocator());
    StyleManager mgr;
    mgr.SetStyleSheet(CSSParser::Parse(core::StringView(u8"label { font-family: Roboto; font-size: 18; }")));
    mgr.SetResourceProvider(&res);
    mgr.ApplyTo(*label.Get());

    CHECK(res.lastFamily.AsView() == core::StringView(u8"Roboto"));
    CHECK(res.lastSize == doctest::Approx(18.0f));
    CHECK(label->GetFont() == &res.font);
}

TEST_CASE("theme: without a provider, resource props are skipped (no crash)")
{
    auto label = ApplyCSS(u8"label { background-image: url(panel); font-family: Roboto; color: #ffffff; }");
    CHECK(label->GetBackground() == nullptr); // no provider -> background-image ignored
    CHECK(label->GetTextColor().r == doctest::Approx(1.0f)); // color still applied
}

TEST_CASE("theme: ParseUrl strips the url() wrapper and quotes")
{
    CHECK(ParseUrl(core::StringView(u8"url(panel)")) == core::StringView(u8"panel"));
    CHECK(ParseUrl(core::StringView(u8"url('a/b.png')")) == core::StringView(u8"a/b.png"));
    CHECK(ParseUrl(core::StringView(u8"plain")) == core::StringView(u8"plain"));
}

TEST_CASE("theme: the built-in default theme restyles the standard widgets")
{
    auto root = Make<SceneNode>();
    auto button = Make<Button>();
    auto textfield = Make<TextField>();
    root->AddChild(button.Get());
    root->AddChild(textfield.Get());

    StyleManager mgr;
    mgr.SetStyleSheet(CSSParser::Parse(DefaultDarkThemeCSS()));
    mgr.ApplyTree(*root.Get());

    // The theme gives the button a background + padding and the textfield padding.
    CHECK(button->GetBackground() != nullptr);
    CHECK(button->GetPadding().Left == doctest::Approx(9.0f));
    CHECK(textfield->GetBackground() != nullptr);
    CHECK(textfield->GetPadding().Left == doctest::Approx(6.0f));

    // Switching to the light theme is just swapping the sheet (hot-reload).
    mgr.SetStyleSheet(CSSParser::Parse(DefaultLightThemeCSS()));
    mgr.ApplyTree(*root.Get());
    CHECK(button->GetBackground() != nullptr); // restyled, still has a bg
}
