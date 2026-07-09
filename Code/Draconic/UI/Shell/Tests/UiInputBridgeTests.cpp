// Tests for UiInputBridge (draconic.ui.shell) - a Draconic reimplementation, covered per the additions
// rule. Synthetic shell::InputEvents drive a UIContext through the bridge; a mock IWindow verifies the
// focus-driven text-input (IME) sync.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.shell;
import draconic.shell;

using namespace draconic::core;
using namespace draconic::ui;
namespace core = draconic::core;
namespace shell = draconic::shell;

namespace
{
    // A RootView-filling focusable/hittable EditText is created directly; no TestHelpers needed here.
    void SetupRoot(UIContext& ctx, RefPtr<RootView>& root)
    {
        root = core::MakeRef<RootView>(core::DefaultAllocator());
        root->ViewportSize = Float2{ 800, 600 };
        ctx.AddRootView(root.Get());
    }

    void LayoutPass(UIContext& ctx, RootView* root)
    {
        ctx.BeginFrame(0.016f);
        ctx.UpdateRootView(root);
    }

    shell::InputEvent MouseDown(f32 x, f32 y)
    {
        shell::InputEvent e{};
        e.kind = shell::InputEventKind::MouseButtonDown;
        e.button = shell::MouseButton::Left;
        e.x = x; e.y = y;
        return e;
    }

    shell::InputEvent TextEvent(const char8_t* s)
    {
        shell::InputEvent e{};
        e.kind = shell::InputEventKind::TextInput;
        usize i = 0;
        for (; s[i] != 0 && i < 31; ++i) { e.text[i] = static_cast<utf8char>(s[i]); }
        e.text[i] = 0;
        return e;
    }

    // Minimal IWindow that records text-input state (all other members stubbed).
    class MockWindow final : public shell::IWindow
    {
    public:
        bool active = false;
        [[nodiscard]] core::u32 Id() const noexcept override { return 1; }
        [[nodiscard]] core::u32 Width() const noexcept override { return 800; }
        [[nodiscard]] core::u32 Height() const noexcept override { return 600; }
        [[nodiscard]] shell::NativeWindow Native() const noexcept override { return {}; }
        [[nodiscard]] bool IsOpen() const noexcept override { return true; }
        [[nodiscard]] bool IsMinimized() const noexcept override { return false; }
        void Close() override {}
        void StartTextInput() override { active = true; }
        void StopTextInput() override { active = false; }
        [[nodiscard]] bool IsTextInputActive() const noexcept override { return active; }
    };
}

TEST_CASE("ui-shell: click focuses and typed text reaches the field")
{
    UIContext ctx; RefPtr<RootView> root; SetupRoot(ctx, root);
    auto edit = core::MakeRef<EditText>(core::DefaultAllocator());
    root->AddView(edit.Get());
    LayoutPass(ctx, root.Get());

    UiInputBridge bridge(&ctx);

    bridge.Dispatch(MouseDown(10, 10)); // hits + focuses the full-window EditText
    CHECK(ctx.WantsTextInput());        // an editable field is focused

    bridge.Dispatch(TextEvent(u8"hi"));
    CHECK(edit->Text() == u8"hi");
}

TEST_CASE("ui-shell: text input target follows focus (IME sync)")
{
    UIContext ctx; RefPtr<RootView> root; SetupRoot(ctx, root);
    auto edit = core::MakeRef<EditText>(core::DefaultAllocator());
    edit->IsReadOnly.SetValue(true); // read-only -> does NOT want text input
    auto edit2 = core::MakeRef<EditText>(core::DefaultAllocator());
    root->AddView(edit.Get());
    root->AddView(edit2.Get());
    LayoutPass(ctx, root.Get());

    MockWindow window;
    UiInputBridge bridge(&ctx);
    bridge.SetTextInputTarget(&window);

    ctx.GetFocusManager()->SetFocus(edit2.Get());
    bridge.SyncTextInput();
    CHECK(window.active); // editable field focused -> IME on

    ctx.GetFocusManager()->SetFocus(edit.Get()); // read-only
    bridge.SyncTextInput();
    CHECK(!window.active); // read-only -> IME off

    ctx.GetFocusManager()->SetFocus(edit2.Get());
    bridge.SyncTextInput();
    CHECK(window.active);

    ctx.GetFocusManager()->ClearFocus();
    bridge.SyncTextInput();
    CHECK(!window.active); // nothing focused -> IME off
}
