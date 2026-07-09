// Ported from Sedulous.UI.Tests/src/ControlTests.bf - the Button/RepeatButton/CheckBox subset (the
// controls ported so far) + the Button/CheckBox OnActivate cases from DirectionalFocusTests. Other
// controls (Label/ToggleButton/Slider/...) land in later batches. Text rendering is deferred in the
// controls, but every tested behavior (state/events/toggle/measure fallback) is exercised here.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
#include "TestHelpers.h"

using namespace draconic::ui;
using namespace draconic::ui::tests;
using namespace draconic::core;
namespace core = draconic::core;

static core::RefPtr<RootView> MakeRoot() { return core::MakeRef<RootView>(core::DefaultAllocator()); }
static core::RefPtr<Button> MakeButton(StringView t) { return core::MakeRef<Button>(core::DefaultAllocator(), t); }
static core::RefPtr<CheckBox> MakeCheckBox(StringView t) { return core::MakeRef<CheckBox>(core::DefaultAllocator(), t); }

// === Button ===

TEST_CASE("control: Button_PressedTransitions")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = MakeButton(u8"Test"); root->AddView(btn.Get()); LayoutPass(ctx, root.Get());

    MouseEventArgs downArgs; downArgs.Set(10, 10, MouseButton::Left);
    btn->OnMouseDown(downArgs);
    CHECK(btn->IsPressed());

    MouseEventArgs upArgs; upArgs.Set(10, 10, MouseButton::Left);
    btn->OnMouseUp(upArgs);
    CHECK(!btn->IsPressed());
}

TEST_CASE("control: Button_DisabledDoesNotClick")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = MakeButton(u8"Test"); btn->IsEnabled = false; root->AddView(btn.Get());
    bool clicked = false;
    btn->OnClick.Add([&clicked](ButtonBase*) { clicked = true; });
    btn->FireClick();
    CHECK(!clicked);
}

TEST_CASE("control: Button_KeyboardActivation")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = MakeButton(u8"Test"); root->AddView(btn.Get());
    bool clicked = false;
    btn->OnClick.Add([&clicked](ButtonBase*) { clicked = true; });
    KeyEventArgs args; args.Set(KeyCode::Return, KeyModifiers::None, false);
    btn->OnKeyDown(args);
    CHECK(clicked);
}

TEST_CASE("control: Button_IsFocusable")
{
    auto btn = MakeButton(u8"Test");
    CHECK(btn->IsFocusable);
    CHECK(btn->IsTabStop);
}

TEST_CASE("control: Button_ControlState_Pressed")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = MakeButton(u8"Test"); root->AddView(btn.Get());
    MouseEventArgs args; args.Set(10, 10, MouseButton::Left);
    btn->OnMouseDown(args);
    CHECK(HasFlag(btn->GetControlState(), ControlState::Pressed));
}

TEST_CASE("control: Button_OnActivate_FiresClick")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = MakeButton(u8"Test"); root->AddView(btn.Get());
    bool clicked = false;
    btn->OnClick.Add([&clicked](ButtonBase*) { clicked = true; });
    btn->OnActivate();
    CHECK(clicked);
}

// === RepeatButton ===

TEST_CASE("control: RepeatButton_ClicksOnce")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = core::MakeRef<RepeatButton>(core::DefaultAllocator(), StringView(u8"Hold")); root->AddView(btn.Get());
    int clickCount = 0;
    btn->OnClick.Add([&clickCount](ButtonBase*) { ++clickCount; });
    KeyEventArgs args; args.Set(KeyCode::Return, KeyModifiers::None, false);
    btn->OnKeyDown(args);
    CHECK(clickCount == 1);
}

TEST_CASE("control: RepeatButton_RepeatsOnHold")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = core::MakeRef<RepeatButton>(core::DefaultAllocator(), StringView(u8"Hold"));
    btn->RepeatDelay = 0.1f; btn->RepeatInterval = 0.05f;
    root->AddView(btn.Get());
    int clickCount = 0;
    btn->OnClick.Add([&clickCount](ButtonBase*) { ++clickCount; });

    MouseEventArgs downArgs; downArgs.Set(10, 10, MouseButton::Left);
    btn->OnMouseDown(downArgs);

    btn->UpdateRepeat(0.05f);
    CHECK(clickCount == 0);
    btn->UpdateRepeat(0.06f); // total 0.11 > 0.1
    CHECK(clickCount >= 1);
    const int countBefore = clickCount;
    btn->UpdateRepeat(0.1f);
    CHECK(clickCount > countBefore);
}

TEST_CASE("control: RepeatButton_StopsOnRelease")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto btn = core::MakeRef<RepeatButton>(core::DefaultAllocator(), StringView(u8"Hold"));
    btn->RepeatDelay = 0.05f; btn->RepeatInterval = 0.02f;
    root->AddView(btn.Get());
    int clickCount = 0;
    btn->OnClick.Add([&clickCount](ButtonBase*) { ++clickCount; });

    MouseEventArgs downArgs; downArgs.Set(10, 10, MouseButton::Left);
    btn->OnMouseDown(downArgs);
    MouseEventArgs upArgs; upArgs.Set(10, 10, MouseButton::Left);
    btn->OnMouseUp(upArgs);

    const int countAfterRelease = clickCount;
    btn->UpdateRepeat(0.2f);
    CHECK(clickCount == countAfterRelease);
}

// === CheckBox ===

TEST_CASE("control: CheckBox_Toggle")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto cb = MakeCheckBox(u8"Option"); root->AddView(cb.Get());
    CHECK(!cb->IsChecked.Value());
    bool fired = false, newVal = false;
    cb->OnCheckedChanged.Add([&](CheckBox*, bool val) { fired = true; newVal = val; });
    cb->IsChecked.SetValue(true);
    CHECK(fired);
    CHECK(newVal == true);
    CHECK(cb->IsChecked.Value());
}

TEST_CASE("control: CheckBox_MouseToggle")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto cb = MakeCheckBox(u8"Option"); root->AddView(cb.Get());
    MouseEventArgs args; args.Set(5, 5, MouseButton::Left);
    cb->OnMouseDown(args);
    CHECK(cb->IsChecked.Value());
    MouseEventArgs args2; args2.Set(5, 5, MouseButton::Left);
    cb->OnMouseDown(args2);
    CHECK(!cb->IsChecked.Value());
}

TEST_CASE("control: CheckBox_NoChangeNotifyOnSameValue")
{
    auto cb = core::MakeRef<CheckBox>(core::DefaultAllocator(), StringView(u8"Test"), true);
    int fireCount = 0;
    cb->OnCheckedChanged.Add([&fireCount](CheckBox*, bool) { ++fireCount; });
    cb->IsChecked.SetValue(true); // same value
    CHECK(fireCount == 0);
}

TEST_CASE("control: CheckBox_OnActivate_Toggles")
{
    auto cb = MakeCheckBox(u8"Test");
    CHECK(!cb->IsChecked.Value());
    cb->OnActivate();
    CHECK(cb->IsChecked.Value());
    cb->OnActivate();
    CHECK(!cb->IsChecked.Value());
}
