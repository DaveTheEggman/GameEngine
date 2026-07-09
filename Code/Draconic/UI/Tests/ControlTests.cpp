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

// === Label ===

TEST_CASE("control: Label_SetText")
{
    auto label = core::MakeRef<Label>(core::DefaultAllocator(), StringView(u8"Hello"));
    CHECK(label->Text.Value() == StringView(u8"Hello"));
}

TEST_CASE("control: Label_SetTextChaining")
{
    auto label = core::MakeRef<Label>(core::DefaultAllocator());
    label->SetText(u8"World");
    CHECK(label->Text.Value() == StringView(u8"World"));
}

TEST_CASE("control: Label_MeasuresNonZero")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto label = core::MakeRef<Label>(core::DefaultAllocator(), StringView(u8"Hello"));
    root->AddView(label.Get());
    LayoutPass(ctx, root.Get());
    CHECK(label->MeasuredSize.y > 0);
}

// === Spacer ===

TEST_CASE("control: Spacer_MeasuresToDesiredSize")
{
    auto spacer = core::MakeRef<Spacer>(core::DefaultAllocator(), 20.0f, 10.0f);
    spacer->Measure(BoxConstraints::Expand());
    CHECK(spacer->MeasuredSize.x == 20);
    CHECK(spacer->MeasuredSize.y == 10);
}

// === ColorView ===

TEST_CASE("control: ColorView_StoresColor")
{
    auto cv = core::MakeRef<ColorView>(core::DefaultAllocator(), core::Color{ 1.0f, 0.0f, 0.0f, 1.0f });
    CHECK(cv->Color.Value().r == 1.0f);
    CHECK(cv->Color.Value().g == 0.0f);
}

// === Separator ===

TEST_CASE("control: Separator_HorizontalMeasure")
{
    auto sep = core::MakeRef<Separator>(core::DefaultAllocator(), Orientation::Horizontal);
    sep->Measure(BoxConstraints::Loose(400, 300));
    CHECK(sep->MeasuredSize.y == 1);
    CHECK(sep->MeasuredSize.x == 400);
}

TEST_CASE("control: Separator_VerticalMeasure")
{
    auto sep = core::MakeRef<Separator>(core::DefaultAllocator(), Orientation::Vertical);
    sep->Measure(BoxConstraints::Loose(400, 300));
    CHECK(sep->MeasuredSize.x == 1);
    CHECK(sep->MeasuredSize.y == 300);
}

// === ProgressBar ===

TEST_CASE("control: ProgressBar_ValueClamped")
{
    auto bar = core::MakeRef<ProgressBar>(core::DefaultAllocator());
    bar->Value.SetValue(0.5f);
    CHECK(bar->Value.Value() == 0.5f);
    bar->Value.SetValue(-1.0f);
    CHECK(bar->Value.Value() == 0.0f);
    bar->Value.SetValue(2.0f);
    CHECK(bar->Value.Value() == 1.0f);
}

// === Panel ===

TEST_CASE("control: Panel_ChildFillsContent")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto panel = core::MakeRef<Panel>(core::DefaultAllocator());
    panel->Padding = Thickness{ 10.0f };
    auto child = core::MakeRef<TestView>(core::DefaultAllocator(), 50.0f, 30.0f);
    panel->AddView(child.Get());
    root->AddView(panel.Get());
    LayoutPass(ctx, root.Get());
    CHECK(child->Bounds.x == doctest::Approx(10));
    CHECK(child->Bounds.y == doctest::Approx(10));
}

// === ImageView ===

TEST_CASE("control: ImageView_NullImage_ZeroSize")
{
    auto iv = core::MakeRef<ImageView>(core::DefaultAllocator());
    iv->Measure(BoxConstraints::Expand());
    CHECK(iv->MeasuredSize.x == 0);
    CHECK(iv->MeasuredSize.y == 0);
}

// === ToggleButton ===

TEST_CASE("control: ToggleButton_Toggle")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto toggle = core::MakeRef<ToggleButton>(core::DefaultAllocator(), StringView(u8"Toggle"));
    root->AddView(toggle.Get());
    CHECK(!toggle->IsChecked.Value());
    bool fired = false;
    toggle->OnCheckedChanged.Add([&fired](ToggleButton*, bool) { fired = true; });
    KeyEventArgs args; args.Set(KeyCode::Space, KeyModifiers::None, false);
    toggle->OnKeyDown(args);
    CHECK(toggle->IsChecked.Value());
    CHECK(fired);
}

// === RadioButton + RadioGroup ===

TEST_CASE("control: RadioButton_CannotUncheckByClick")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto radio = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"Option"));
    radio->IsChecked.SetValue(true);
    root->AddView(radio.Get());
    MouseEventArgs args; args.Set(5, 5, MouseButton::Left);
    radio->OnMouseDown(args);
    CHECK(radio->IsChecked.Value()); // still checked
}

TEST_CASE("control: RadioGroup_MutualExclusion")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto group = core::MakeRef<RadioGroup>(core::DefaultAllocator());
    auto a = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"A"));
    auto b = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"B"));
    auto c = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"C"));
    group->AddRadioButton(a.Get()); group->AddRadioButton(b.Get()); group->AddRadioButton(c.Get());
    root->AddView(group.Get());

    group->CheckAt(0);
    CHECK(a->IsChecked.Value());
    CHECK(!b->IsChecked.Value());

    b->IsChecked.SetValue(true);
    CHECK(!a->IsChecked.Value());
    CHECK(b->IsChecked.Value());
    CHECK(!c->IsChecked.Value());
}

TEST_CASE("control: RadioGroup_SelectionChangedEvent")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto group = core::MakeRef<RadioGroup>(core::DefaultAllocator());
    auto a = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"A"));
    auto b = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"B"));
    group->AddRadioButton(a.Get()); group->AddRadioButton(b.Get());
    root->AddView(group.Get());

    RadioButton* selected = nullptr;
    group->OnSelectionChanged.Add([&selected](RadioGroup*, RadioButton* r) { selected = r; });

    a->IsChecked.SetValue(true);
    CHECK(selected == a.Get());
    b->IsChecked.SetValue(true);
    CHECK(selected == b.Get());
}

// === ToggleSwitch ===

TEST_CASE("control: ToggleSwitch_Toggle")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto sw = core::MakeRef<ToggleSwitch>(core::DefaultAllocator(), StringView(u8"VSync"));
    root->AddView(sw.Get());
    CHECK(!sw->IsChecked.Value());
    bool toggled = false;
    sw->OnCheckedChanged.Add([&toggled](ToggleSwitch*, bool) { toggled = true; });
    MouseEventArgs args; args.Set(10, 10, MouseButton::Left);
    sw->OnMouseDown(args);
    CHECK(sw->IsChecked.Value());
    CHECK(toggled);
}

TEST_CASE("control: ToggleSwitch_OnActivate_Toggles")
{
    auto sw = core::MakeRef<ToggleSwitch>(core::DefaultAllocator(), StringView(u8"Test"));
    CHECK(!sw->IsChecked.Value());
    sw->OnActivate();
    CHECK(sw->IsChecked.Value());
}

TEST_CASE("control: RadioButton_OnActivate_Selects")
{
    auto rb = core::MakeRef<RadioButton>(core::DefaultAllocator(), StringView(u8"Test"));
    CHECK(!rb->IsChecked.Value());
    rb->OnActivate();
    CHECK(rb->IsChecked.Value());
}
