// Draconic GUI - TextField tests: the keyboard/text-input path end-to-end. Text arrives via
// the EventDispatcher's InjectText (routed to the focused node), editing keys via InjectKeyDown.
// A mock font (6px/byte advance) drives caret-placement measurement.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.fonts;
import draconic.vg;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;
namespace fonts = draconic::fonts;
namespace vg = draconic::vg;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }

    // 6px advance per byte, 12px line height (mirrors the Text-test mock).
    class MockFont : public fonts::IFont
    {
    public:
        core::StringView FamilyName() const override { return core::StringView(u8"mock"); }
        fonts::FontMetrics Metrics() const override
        {
            fonts::FontMetrics m;
            m.ascent = 10.0f; m.descent = -2.0f; m.lineGap = 0.0f;
            m.lineHeight = 12.0f; m.pixelHeight = 10.0f; m.scale = 1.0f;
            return m;
        }
        core::f32 PixelHeight() const override { return 10.0f; }
        fonts::GlyphInfo GetGlyphInfo(core::i32 cp) const override
        {
            fonts::GlyphInfo g; g.codepoint = cp; g.advanceWidth = 6.0f; return g;
        }
        core::f32 GetKerning(core::i32, core::i32) const override { return 0.0f; }
        bool HasGlyph(core::i32) const override { return true; }
        core::f32 MeasureString(core::StringView text) const override { return static_cast<core::f32>(text.Size()) * 6.0f; }
        core::f32 MeasureString(core::StringView text, core::Array<fonts::GlyphPosition>& out) const override
        {
            (void)out; return static_cast<core::f32>(text.Size()) * 6.0f;
        }
    };

    MockFont* NewMock() { return core::DefaultAllocator().New<MockFont>(); }

    // u32 view of a gui::KeyCode, since InjectKeyDown takes a raw code.
    core::u32 Key(KeyCode k) { return static_cast<core::u32>(k); }
}

TEST_CASE("textfield: typed text is inserted at the caret and the caret advances")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 100.0f });
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    // Focus the field (text only routes to the focused node).
    f->RequestFocus();
    CHECK(f->IsFocused());

    d->InjectText(core::StringView(u8"Hi"));
    CHECK(f->GetText() == core::StringView(u8"Hi"));
    CHECK(f->GetCaret() == 2);

    // Move the caret to the start and insert there.
    d->InjectKeyDown(Key(KeyCode::Home));
    CHECK(f->GetCaret() == 0);
    d->InjectText(core::StringView(u8"Oh "));
    CHECK(f->GetText() == core::StringView(u8"Oh Hi"));
    CHECK(f->GetCaret() == 3);
}

TEST_CASE("textfield: text only reaches a focused field")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 100.0f });
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    d->InjectText(core::StringView(u8"nope")); // not focused -> dropped
    CHECK(f->GetText().Size() == 0);
}

TEST_CASE("textfield: backspace and delete remove whole codepoints (UTF-8)")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 100.0f });
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();
    f->RequestFocus();

    f->SetText(core::StringView(u8"aé")); // 'a'(1) + 'é'(2 bytes), caret at end (3)
    CHECK(f->GetCaret() == 3);

    d->InjectKeyDown(Key(KeyCode::Backspace)); // removes the whole 'é'
    CHECK(f->GetText() == core::StringView(u8"a"));
    CHECK(f->GetCaret() == 1);

    d->InjectKeyDown(Key(KeyCode::Home));
    d->InjectKeyDown(Key(KeyCode::Delete)); // removes 'a' at the front
    CHECK(f->GetText().Size() == 0);
    CHECK(f->GetCaret() == 0);

    // Backspace at the start and delete at the end are no-ops.
    d->InjectKeyDown(Key(KeyCode::Backspace));
    d->InjectKeyDown(Key(KeyCode::Delete));
    CHECK(f->GetText().Size() == 0);
}

TEST_CASE("textfield: caret navigation steps by codepoint and clamps")
{
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    auto root = Make<SceneNode>();
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();
    f->RequestFocus();

    f->SetText(core::StringView(u8"é€")); // 2 bytes + 3 bytes = 5 bytes, caret at 5
    d->InjectKeyDown(Key(KeyCode::Home));
    CHECK(f->GetCaret() == 0);
    d->InjectKeyDown(Key(KeyCode::Right)); // over 'é'
    CHECK(f->GetCaret() == 2);
    d->InjectKeyDown(Key(KeyCode::Right)); // over '€'
    CHECK(f->GetCaret() == 5);
    d->InjectKeyDown(Key(KeyCode::Right)); // clamp at end
    CHECK(f->GetCaret() == 5);
    d->InjectKeyDown(Key(KeyCode::Left));  // back over '€'
    CHECK(f->GetCaret() == 2);
    d->InjectKeyDown(Key(KeyCode::End));
    CHECK(f->GetCaret() == 5);
}

TEST_CASE("textfield: change callback fires on edits")
{
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    auto root = Make<SceneNode>();
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();
    f->RequestFocus();

    int changes = 0;
    core::String last;
    f->SetOnTextChanged([&](core::StringView v) { ++changes; last = core::String(v); });

    d->InjectText(core::StringView(u8"x"));
    CHECK(changes == 1);
    CHECK(last.AsView() == core::StringView(u8"x"));
    d->InjectKeyDown(Key(KeyCode::Backspace));
    CHECK(changes == 2);
    CHECK(last.Size() == 0);

    // A navigation key does not fire the change callback.
    const int before = changes;
    d->InjectKeyDown(Key(KeyCode::Home));
    CHECK(changes == before);
}

TEST_CASE("textfield: mouse press focuses and places the caret at the nearest boundary")
{
    fonts::CachedFont cf(NewMock(), nullptr, nullptr); // 6px/byte
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 100.0f });
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    f->SetFont(&cf);
    f->SetText(core::StringView(u8"abcd")); // widths at boundaries: 0,6,12,18,24
    root->AddChild(f.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    // Click near x=13 (content x, no padding) -> closest boundary is offset 2 (width 12).
    d->InjectMouseDown(core::Float2{ 13.0f, 12.0f }, MouseButton::Left);
    CHECK(f->IsFocused());
    CHECK(f->GetCaret() == 2);

    // Click past the end -> caret at end.
    d->InjectMouseDown(core::Float2{ 90.0f, 12.0f }, MouseButton::Left);
    CHECK(f->GetCaret() == 4);

    // Click at the far left -> caret at start.
    d->InjectMouseDown(core::Float2{ 0.0f, 12.0f }, MouseButton::Left);
    CHECK(f->GetCaret() == 0);
}

TEST_CASE("textfield: caret draws only while focused")
{
    fonts::CachedFont cf(NewMock(), nullptr, nullptr);
    auto f = Make<TextField>();
    f->SetSize(core::Float2{ 100.0f, 24.0f });
    f->SetFont(&cf);

    // Not focused: no caret geometry (empty text, no font-service atlas -> no glyph geometry).
    {
        vg::VGContext ctx;
        DrawContext dc{ ctx };
        f->Draw(dc);
        CHECK(ctx.GetBatch().vertices.Size() == 0);
    }

    // Focused (via a coordinator root so RequestFocus routes): caret rect is drawn.
    auto root = Make<SceneNode>();
    root->AddChild(f.Get());
    f->RequestFocus();
    {
        vg::VGContext ctx;
        DrawContext dc{ ctx };
        f->Draw(dc);
        CHECK(ctx.GetBatch().vertices.Size() > 0); // the caret quad
    }
}

TEST_CASE("textfield: blink toggles the caret over time while focused")
{
    auto root = Make<SceneNode>();
    auto f = Make<TextField>();
    root->AddChild(f.Get());
    f->RequestFocus(); // ResetBlink -> visible

    // Half a period flips it off, another half flips it back on.
    f->Update(0.5);
    f->Update(0.5);
    // (No direct getter for visibility; exercised for coverage/no-crash and the focused guard.)

    f->ReleaseFocus();
    f->Update(1.0); // ignored while unfocused (no crash, no toggle)
    CHECK_FALSE(f->IsFocused());
}
