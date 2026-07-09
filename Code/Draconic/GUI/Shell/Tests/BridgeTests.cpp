// Draconic GUI - GuiInputBridge tests: translate synthetic platform InputEvents into
// EventDispatcher injections (hover/click/key/text), map enums, and apply a ContentFit.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.shell;
import draconic.gui;
import draconic.gui.shell;

using namespace draconic::gui;
namespace core = draconic::core;
namespace shell = draconic::shell;

namespace
{
    core::RefPtr<SceneNode> MakeScene(core::Float2 size)
    {
        auto s = core::MakeRef<SceneNode>(core::DefaultAllocator());
        s->SetSize(size);
        return s;
    }
    core::RefPtr<Node> MakePanel(core::Float2 pos, core::Float2 size)
    {
        auto n = core::MakeRef<Node>(core::DefaultAllocator());
        n->SetSize(size);
        n->SetPosition(pos);
        return n;
    }

    shell::InputEvent MouseMove(float x, float y)
    {
        shell::InputEvent e;
        e.kind = shell::InputEventKind::MouseMove;
        e.x = x; e.y = y;
        return e;
    }
    shell::InputEvent MakeButtonEvent(shell::InputEventKind kind, float x, float y,
                                  shell::MouseButton button, shell::KeyModifiers mods = shell::KeyModifiers::None)
    {
        shell::InputEvent e;
        e.kind = kind;
        e.x = x; e.y = y; e.button = button; e.modifiers = mods;
        return e;
    }
    shell::InputEvent Key(shell::InputEventKind kind, shell::KeyCode key, shell::KeyModifiers mods = shell::KeyModifiers::None)
    {
        shell::InputEvent e;
        e.kind = kind;
        e.key = key; e.modifiers = mods;
        return e;
    }
    shell::InputEvent Text(const char8_t* s)
    {
        shell::InputEvent e;
        e.kind = shell::InputEventKind::TextInput;
        core::usize i = 0;
        for (; s[i] != 0 && i < 31; ++i) e.text[i] = s[i];
        e.text[i] = 0;
        return e;
    }
}

TEST_CASE("bridge: mouse move drives hover")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 10.0f, 10.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    GuiInputBridge bridge{ root->GetEventDispatcher() };

    int enter = 0;
    child->AddEventListener(EventType::MouseEnter, [&](const Event&) { ++enter; });

    CHECK(bridge.Dispatch(MouseMove(20.0f, 20.0f)));
    CHECK(root->GetEventDispatcher()->GetOverNode() == child.Get());
    CHECK(enter == 1);
}

TEST_CASE("bridge: press+release becomes a click and maps the button")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 0.0f, 0.0f }, core::Float2{ 100.0f, 100.0f });
    root->AddChild(child.Get());
    GuiInputBridge bridge{ root->GetEventDispatcher() };

    int clicks = 0;
    draconic::gui::MouseButton seen = draconic::gui::MouseButton::Left;
    child->AddEventListener(EventType::MouseClick,
        [&](const Event& e) { ++clicks; seen = static_cast<const MouseEvent&>(e).Button; });

    bridge.Dispatch(MakeButtonEvent(shell::InputEventKind::MouseButtonDown, 20.0f, 20.0f, shell::MouseButton::Right));
    bridge.Dispatch(MakeButtonEvent(shell::InputEventKind::MouseButtonUp, 20.0f, 20.0f, shell::MouseButton::Right));
    CHECK(clicks == 1);
    CHECK(seen == draconic::gui::MouseButton::Right); // shell Right -> gui Right (not by value)
    CHECK(root->GetEventDispatcher()->GetFocusNode() == child.Get());
}

TEST_CASE("bridge: key and text route to the focus node with mapped modifiers")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 0.0f, 0.0f }, core::Float2{ 100.0f, 100.0f });
    root->AddChild(child.Get());
    GuiInputBridge bridge{ root->GetEventDispatcher() };
    root->GetEventDispatcher()->SetFocusNode(child.Get());

    unsigned seenMods = 0;
    core::StringView seenText;
    child->AddEventListener(EventType::KeyDown,
        [&](const Event& e) { seenMods = static_cast<const KeyEvent&>(e).Modifiers; });
    child->AddEventListener(EventType::TextInput,
        [&](const Event& e) { seenText = static_cast<const TextInputEvent&>(e).Text; });

    bridge.Dispatch(Key(shell::InputEventKind::KeyDown, shell::KeyCode::A, shell::KeyModifiers::LeftCtrl));
    bridge.Dispatch(Text(u8"hi"));
    CHECK((seenMods & KeyModCtrl) != 0u);
    CHECK(seenText == core::StringView(u8"hi"));
}

TEST_CASE("bridge: content fit maps window position into content space")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 10.0f, 10.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    GuiInputBridge bridge{ root->GetEventDispatcher() };

    core::ContentFit fit;
    fit.region = core::Rectangle{ 0.0f, 0.0f, 400.0f, 400.0f };
    fit.contentSize = core::Float2{ 200.0f, 200.0f };
    fit.mode = core::FitMode::Stretch;
    bridge.SetContentFit(fit);

    bridge.Dispatch(MouseMove(40.0f, 40.0f)); // window (40,40) -> content (20,20), inside child
    CHECK(root->GetEventDispatcher()->GetOverNode() == child.Get());
    CHECK(root->GetEventDispatcher()->GetMousePosition().x == doctest::Approx(20.0f));
}

TEST_CASE("bridge: unroutable events return false")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    GuiInputBridge bridge{ root->GetEventDispatcher() };

    shell::InputEvent gamepad;
    gamepad.kind = shell::InputEventKind::GamepadButtonDown;
    CHECK_FALSE(bridge.Dispatch(gamepad));
}
