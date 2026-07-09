// Draconic GUI - EventDispatcher tests: hover enter/leave, click, focus (click + program-
// matic), key/text routing to the focus node, and interaction-ref cleanup. Input is
// injected as abstract events (the shell bridge's job), hit-tested via the tree's OverFind.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

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
}

TEST_CASE("dispatch: hover enter/leave tracks the node under the cursor")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 10.0f, 10.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    int enter = 0, leave = 0;
    child->AddEventListener(EventType::MouseEnter, [&](const Event&) { ++enter; });
    child->AddEventListener(EventType::MouseLeave, [&](const Event&) { ++leave; });

    d->InjectMouseMove(core::Float2{ 20.0f, 20.0f }); // over child
    CHECK(d->GetOverNode() == child.Get());
    CHECK(enter == 1);
    CHECK(leave == 0);

    d->InjectMouseMove(core::Float2{ 120.0f, 120.0f }); // off child, over root
    CHECK(d->GetOverNode() == root.Get());
    CHECK(leave == 1);
}

TEST_CASE("dispatch: press+release on the same node is a click and focuses it")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 10.0f, 10.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    int clicks = 0;
    child->AddEventListener(EventType::MouseClick, [&](const Event&) { ++clicks; });

    d->InjectMouseDown(core::Float2{ 20.0f, 20.0f }, MouseButton::Left);
    d->InjectMouseUp(core::Float2{ 20.0f, 20.0f }, MouseButton::Left);
    CHECK(clicks == 1);
    CHECK(d->GetFocusNode() == child.Get());
    CHECK(child->IsFocused());
}

TEST_CASE("dispatch: mouse event payload is accessible via static_cast")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    EventDispatcher* d = root->GetEventDispatcher();

    core::Float2 seen{ -1.0f, -1.0f };
    root->AddEventListener(EventType::MouseMove,
        [&](const Event& e) { seen = static_cast<const MouseEvent&>(e).Position; });

    d->InjectMouseMove(core::Float2{ 33.0f, 44.0f });
    CHECK(seen.x == doctest::Approx(33.0f));
    CHECK(seen.y == doctest::Approx(44.0f));
}

TEST_CASE("dispatch: key and text route to the focus node")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 0.0f, 0.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    int keys = 0, texts = 0;
    core::StringView lastText;
    child->AddEventListener(EventType::KeyDown, [&](const Event&) { ++keys; });
    child->AddEventListener(EventType::TextInput,
        [&](const Event& e) { ++texts; lastText = static_cast<const TextInputEvent&>(e).Text; });

    d->InjectKeyDown(65); // no focus yet -> dropped
    CHECK(keys == 0);

    d->SetFocusNode(child.Get());
    d->InjectKeyDown(65);
    d->InjectText(core::StringView(u8"hi"));
    CHECK(keys == 1);
    CHECK(texts == 1);
    CHECK(lastText == core::StringView(u8"hi"));
}

TEST_CASE("dispatch: programmatic focus gains and releases")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto a = MakePanel(core::Float2{ 0.0f, 0.0f }, core::Float2{ 50.0f, 50.0f });
    auto b = MakePanel(core::Float2{ 60.0f, 0.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(a.Get());
    root->AddChild(b.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    a->RequestFocus();
    CHECK(d->GetFocusNode() == a.Get());
    CHECK(a->IsFocused());

    b->RequestFocus(); // moves focus: a loses, b gains
    CHECK(d->GetFocusNode() == b.Get());
    CHECK_FALSE(a->IsFocused());
    CHECK(b->IsFocused());

    b->ReleaseFocus();
    CHECK(d->GetFocusNode() == nullptr);
    CHECK_FALSE(b->IsFocused());
}

TEST_CASE("dispatch: NotifyNodeRemoved clears interaction refs")
{
    auto root = MakeScene(core::Float2{ 200.0f, 200.0f });
    auto child = MakePanel(core::Float2{ 0.0f, 0.0f }, core::Float2{ 50.0f, 50.0f });
    root->AddChild(child.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    d->InjectMouseMove(core::Float2{ 10.0f, 10.0f });
    d->SetFocusNode(child.Get());
    CHECK(d->GetOverNode() == child.Get());
    CHECK(d->GetFocusNode() == child.Get());

    d->NotifyNodeRemoved(child.Get());
    CHECK(d->GetOverNode() == nullptr);
    CHECK(d->GetFocusNode() == nullptr);
}
