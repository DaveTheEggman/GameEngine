// draconic.input tests: model round-trip (binary + XML) + validation, and ActionRuntime
// evaluation over SYNTHETIC devices (the design's testing contract - no real hardware).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.shell;
import draconic.input;
import draconic.settings;
import draconic.script;
import draconic.script.wren;
import draconic.input.subsystem;
import draconic.xml;
import draconic.xml.serialization;

using namespace draconic::core;
using namespace draconic::input;
namespace dshell = draconic::shell;

namespace
{
    // ---- synthetic devices --------------------------------------------------------------
    class FakeKeyboard final : public dshell::IKeyboard
    {
    public:
        bool down[512] = {};
        bool pressed[512] = {};
        dshell::KeyModifiers mods = dshell::KeyModifiers::None;
        [[nodiscard]] bool IsKeyDown(dshell::KeyCode key) const override { return down[static_cast<u32>(key) & 511]; }
        [[nodiscard]] bool IsKeyPressed(dshell::KeyCode key) const override { return pressed[static_cast<u32>(key) & 511]; }
        [[nodiscard]] bool IsKeyReleased(dshell::KeyCode) const override { return false; }
        [[nodiscard]] dshell::KeyModifiers Modifiers() const override { return mods; }
        void Set(dshell::KeyCode key, bool value) { down[static_cast<u32>(key) & 511] = value; }
    };

    class FakeMouse final : public dshell::IMouse
    {
    public:
        f32 dx = 0, dy = 0, wheel = 0;
        bool buttons[8] = {};
        [[nodiscard]] f32 X() const override { return 0; }
        [[nodiscard]] f32 Y() const override { return 0; }
        [[nodiscard]] f32 GlobalX() const override { return 0; }
        [[nodiscard]] f32 GlobalY() const override { return 0; }
        [[nodiscard]] f32 DeltaX() const override { return dx; }
        [[nodiscard]] f32 DeltaY() const override { return dy; }
        [[nodiscard]] f32 ScrollX() const override { return 0; }
        [[nodiscard]] f32 ScrollY() const override { return wheel; }
        [[nodiscard]] bool IsButtonDown(dshell::MouseButton b) const override { return buttons[static_cast<u32>(b) & 7]; }
        [[nodiscard]] bool IsButtonPressed(dshell::MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonReleased(dshell::MouseButton) const override { return false; }
        [[nodiscard]] bool RelativeMode() const override { return false; }
        void SetRelativeMode(bool) override {}
        [[nodiscard]] bool CursorVisible() const override { return true; }
        void SetCursorVisible(bool) override {}
        void SetCursor(dshell::CursorType) override {}
        void SetGlobalCapture(bool) override {}
    };

    class FakeGamepad final : public dshell::IGamepad
    {
    public:
        i32 index = 0;
        bool connected = true;
        bool buttons[32] = {};
        f32 axes[6] = {};
        [[nodiscard]] i32 Index() const override { return index; }
        [[nodiscard]] StringView Name() const override { return u8"fake"; }
        [[nodiscard]] bool Connected() const override { return connected; }
        bool buttonsPressed[32] = {};
        [[nodiscard]] bool IsButtonDown(dshell::GamepadButton b) const override { return buttons[static_cast<u32>(b) & 31]; }
        [[nodiscard]] bool IsButtonPressed(dshell::GamepadButton b) const override { return buttonsPressed[static_cast<u32>(b) & 31]; }
        [[nodiscard]] bool IsButtonReleased(dshell::GamepadButton) const override { return false; }
        [[nodiscard]] f32 Axis(dshell::GamepadAxis a) const override { return axes[static_cast<u32>(a) % 6]; }
        void SetRumble(f32, f32, u32) override {}
    };

    class FakeDevices final : public IInputSourceProvider
    {
    public:
        FakeKeyboard keyboard;
        FakeMouse mouse;
        Array<FakeGamepad*> pads;
        [[nodiscard]] dshell::IKeyboard* Keyboard() override { return &keyboard; }
        [[nodiscard]] dshell::IMouse* Mouse() override { return &mouse; }
        [[nodiscard]] i32 GamepadCount() const override { return static_cast<i32>(pads.Size()); }
        [[nodiscard]] dshell::IGamepad* Gamepad(i32 i) override
        {
            return (i >= 0 && i < static_cast<i32>(pads.Size())) ? pads[static_cast<usize>(i)] : nullptr;
        }
    };

    // ---- a representative map ----------------------------------------------------------
    [[nodiscard]] InputMap MakeGameplayMap()
    {
        InputMap map;
        ActionSet gameplay;
        gameplay.name = String(u8"Gameplay");
        gameplay.priority = 0;
        {
            Action jump;
            jump.name = String(u8"Jump");
            jump.kind = ActionKind::Button;
            Binding key;
            key.source = BindingSource::Key;
            key.code = static_cast<u32>(dshell::KeyCode::Space);
            jump.bindings.PushBack(key);
            Binding pad;
            pad.source = BindingSource::GamepadButton;
            pad.code = 0;   // "south" button
            jump.bindings.PushBack(pad);
            gameplay.actions.PushBack(static_cast<Action&&>(jump));
        }
        {
            Action move;
            move.name = String(u8"Move");
            move.kind = ActionKind::Axis2D;
            Binding wasd;
            wasd.source = BindingSource::Composite2D;
            wasd.negX = static_cast<u32>(dshell::KeyCode::A);
            wasd.posX = static_cast<u32>(dshell::KeyCode::D);
            wasd.negY = static_cast<u32>(dshell::KeyCode::S);
            wasd.posY = static_cast<u32>(dshell::KeyCode::W);
            move.bindings.PushBack(wasd);
            Binding stick;
            stick.source = BindingSource::GamepadStick;
            stick.code = static_cast<u32>(StickCode::Left);
            stick.deadZone = 0.2f;
            move.bindings.PushBack(stick);
            gameplay.actions.PushBack(static_cast<Action&&>(move));
        }
        map.sets.PushBack(static_cast<ActionSet&&>(gameplay));

        ActionSet menu;
        menu.name = String(u8"Menu");
        menu.priority = 10;
        {
            Action confirm;
            confirm.name = String(u8"Confirm");
            confirm.kind = ActionKind::Button;
            Binding key;
            key.source = BindingSource::Key;
            key.code = static_cast<u32>(dshell::KeyCode::Space);   // deliberately shared with Jump
            confirm.bindings.PushBack(key);
            menu.actions.PushBack(static_cast<Action&&>(confirm));
        }
        map.sets.PushBack(static_cast<ActionSet&&>(menu));
        return map;
    }
}

TEST_CASE("input: map round-trips through binary AND xml serializers")
{
    InputMap map = MakeGameplayMap();
    map.sets[0].actions[1].processors.sensitivity = 4.0f;
    map.sets[0].actions[1].processors.gravity = 8.0f;
    map.sets[0].actions[1].processors.snap = true;

    auto verify = [&](InputMap& loaded) {
        REQUIRE(loaded.sets.Size() == 2);
        CHECK(loaded.sets[0].name == u8"Gameplay");
        CHECK(loaded.sets[1].priority == 10);
        REQUIRE(loaded.sets[0].actions.Size() == 2);
        const Action& move = loaded.sets[0].actions[1];
        CHECK(move.kind == ActionKind::Axis2D);
        REQUIRE(move.bindings.Size() == 2);
        CHECK(move.bindings[0].source == BindingSource::Composite2D);
        CHECK(move.bindings[0].posY == static_cast<u32>(dshell::KeyCode::W));
        CHECK(move.bindings[1].deadZone == doctest::Approx(0.2f));
        CHECK(move.processors.sensitivity == doctest::Approx(4.0f));
        CHECK(move.processors.snap);
    };

    {
        MemoryStream buffer;
        {
            BinarySerializer ar(buffer, SerializeMode::Write);
            SerializeInputMap(ar, map);
            REQUIRE(ar.IsOk());
        }
        (void)buffer.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(buffer, SerializeMode::Read);
        InputMap loaded;
        SerializeInputMap(ar, loaded);
        REQUIRE(ar.IsOk());
        verify(loaded);
    }
    {
        draconic::xml::XmlSerializer writer;
        SerializeInputMap(writer, map);
        REQUIRE(writer.IsOk());
        String text;
        writer.GetOutput(text);
        draconic::xml::XmlDocument doc;
        REQUIRE(doc.Parse(text.AsView()) == draconic::xml::XmlResult::Ok);
        draconic::xml::XmlSerializer reader(doc);
        InputMap loaded;
        SerializeInputMap(reader, loaded);
        REQUIRE(reader.IsOk());
        verify(loaded);
    }
}

TEST_CASE("input: validation refuses kind mismatches and empty names")
{
    InputMap map = MakeGameplayMap();
    CHECK(ValidateInputMap(map));

    InputMap bad = MakeGameplayMap();
    Binding stick;
    stick.source = BindingSource::GamepadStick;
    bad.sets[0].actions[0].bindings.PushBack(stick);   // 2D binding on the Jump BUTTON
    String error;
    CHECK_FALSE(ValidateInputMap(bad, &error));
    CHECK(!error.IsEmpty());

    InputMap nameless = MakeGameplayMap();
    nameless.sets[0].actions[0].name = String{};
    CHECK_FALSE(ValidateInputMap(nameless));
}

TEST_CASE("input: buttons - edges, device folding, and key modifiers")
{
    ActionRuntime runtime;
    runtime.SetMap(MakeGameplayMap());
    runtime.DisableSet(u8"Menu");   // Space resolves to Gameplay/Jump
    FakeDevices devices;
    FakeGamepad pad;
    devices.pads.PushBack(&pad);
    const ActionRef jump = runtime.Resolve(u8"Jump");
    REQUIRE(jump.IsValid());

    runtime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(jump));

    // Keyboard press: down + a pressed edge exactly this frame, held next frame.
    devices.keyboard.Set(dshell::KeyCode::Space, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));
    CHECK(runtime.WasPressed(jump));
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));
    CHECK_FALSE(runtime.WasPressed(jump));

    // Release edge; then the GAMEPAD binding drives the same action (device folding).
    devices.keyboard.Set(dshell::KeyCode::Space, false);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(jump));
    CHECK(runtime.WasReleased(jump));
    pad.buttons[0] = true;
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));
    pad.buttons[0] = false;

    // A modifier-gated binding only fires with the modifier held.
    InputMap modMap;
    ActionSet set;
    set.name = String(u8"S");
    Action save;
    save.name = String(u8"QuickSave");
    save.kind = ActionKind::Button;
    Binding ctrlS;
    ctrlS.source = BindingSource::Key;
    ctrlS.code = static_cast<u32>(dshell::KeyCode::S);
    ctrlS.modifiers = static_cast<u32>(dshell::KeyModifiers::LeftCtrl);
    save.bindings.PushBack(ctrlS);
    set.actions.PushBack(static_cast<Action&&>(save));
    modMap.sets.PushBack(static_cast<ActionSet&&>(set));
    ActionRuntime modRuntime;
    modRuntime.SetMap(modMap);
    const ActionRef quickSave = modRuntime.Resolve(u8"QuickSave");
    devices.keyboard.Set(dshell::KeyCode::S, true);
    modRuntime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(modRuntime.IsDown(quickSave));
    devices.keyboard.mods = dshell::KeyModifiers::LeftCtrl;
    modRuntime.Update(devices, 1.0f / 60.0f);
    CHECK(modRuntime.IsDown(quickSave));
    devices.keyboard.Set(dshell::KeyCode::S, false);
    devices.keyboard.mods = dshell::KeyModifiers::None;
}

TEST_CASE("input: axes - composite, stick dead zone, and folding by magnitude")
{
    ActionRuntime runtime;
    runtime.SetMap(MakeGameplayMap());
    runtime.DisableSet(u8"Menu");
    FakeDevices devices;
    FakeGamepad pad;
    devices.pads.PushBack(&pad);
    const ActionRef move = runtime.Resolve(u8"Move");

    // WASD: diagonal normalizes to unit length.
    devices.keyboard.Set(dshell::KeyCode::W, true);
    devices.keyboard.Set(dshell::KeyCode::D, true);
    runtime.Update(devices, 1.0f / 60.0f);
    Float2 v = runtime.Value2D(move);
    CHECK(v.x == doctest::Approx(0.7071f).epsilon(0.01));
    CHECK(v.y == doctest::Approx(0.7071f).epsilon(0.01));
    CHECK(runtime.IsDown(move));
    devices.keyboard.Set(dshell::KeyCode::W, false);
    devices.keyboard.Set(dshell::KeyCode::D, false);

    // Stick inside the dead zone reads zero; outside it rescales from the edge.
    pad.axes[static_cast<u32>(dshell::GamepadAxis::LeftX)] = 0.1f;
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.Value2D(move).x == doctest::Approx(0.0f));
    pad.axes[static_cast<u32>(dshell::GamepadAxis::LeftX)] = 1.0f;
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.Value2D(move).x == doctest::Approx(1.0f).epsilon(0.01));

    // Keyboard (0.7) vs stick (1.0): the larger magnitude wins the fold.
    devices.keyboard.Set(dshell::KeyCode::A, true);   // x = -1 from keys, +1 from stick... stick larger? equal
    pad.axes[static_cast<u32>(dshell::GamepadAxis::LeftX)] = 0.4f;
    runtime.Update(devices, 1.0f / 60.0f);
    v = runtime.Value2D(move);
    CHECK(v.x == doctest::Approx(-1.0f).epsilon(0.01));   // the full-press key out-magnitudes 0.25-ish stick
    devices.keyboard.Set(dshell::KeyCode::A, false);
    pad.axes[static_cast<u32>(dshell::GamepadAxis::LeftX)] = 0.0f;
}

TEST_CASE("input: exclusive sets - priority resolution, suppression, and held latching")
{
    ActionRuntime runtime;
    runtime.SetMap(MakeGameplayMap());
    FakeDevices devices;
    const ActionRef jump = runtime.Resolve(u8"Jump");
    const ActionRef confirm = runtime.Resolve(u8"Confirm");

    // Hold Space in gameplay: Jump is down.
    devices.keyboard.Set(dshell::KeyCode::Space, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));

    // Menu goes exclusive mid-hold: Jump releases (edge fires once), Confirm must NOT
    // fire from the stale hold... it is latched too? No: Confirm was suppressed BEFORE
    // (not top set? it evaluates now) - Confirm became physically pressed only while its
    // set was suppressed-inactive, so it latched and stays released until a re-press.
    runtime.PushExclusiveSet(u8"Menu");
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(jump));
    CHECK(runtime.WasReleased(jump));
    CHECK_FALSE(runtime.IsDown(confirm));   // latched: held from before the menu opened

    // Releasing and re-pressing INSIDE the menu activates Confirm, not Jump.
    devices.keyboard.Set(dshell::KeyCode::Space, false);
    runtime.Update(devices, 1.0f / 60.0f);
    devices.keyboard.Set(dshell::KeyCode::Space, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(confirm));
    CHECK(runtime.WasPressed(confirm));
    CHECK_FALSE(runtime.IsDown(jump));

    // Menu closes while Space is STILL held: Jump stays released (latched) until the
    // physical release; the next press works normally.
    runtime.PopExclusiveSet();
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(jump));   // the held key must not re-fire
    devices.keyboard.Set(dshell::KeyCode::Space, false);
    runtime.Update(devices, 1.0f / 60.0f);
    devices.keyboard.Set(dshell::KeyCode::Space, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));
    CHECK(runtime.WasPressed(jump));
}

TEST_CASE("input: rebind overlay - apply over a pristine copy, clear restores, persists")
{
    RegisterInputTypes();
    InputMap asset = MakeGameplayMap();

    // Override: Jump moves from Space to J.
    InputBindingOverrides overlay;
    {
        Array<Binding> replacement;
        Binding j;
        j.source = BindingSource::Key;
        j.code = static_cast<u32>(dshell::KeyCode::J);
        replacement.PushBack(j);
        overlay.Set(u8"Gameplay", u8"Jump", static_cast<Array<Binding>&&>(replacement));
    }

    InputMap effective = asset;   // pristine copy
    ApplyBindingOverrides(effective, overlay);
    REQUIRE(effective.sets[0].actions[0].bindings.Size() == 1);
    CHECK(effective.sets[0].actions[0].bindings[0].code == static_cast<u32>(dshell::KeyCode::J));
    CHECK(asset.sets[0].actions[0].bindings.Size() == 2);   // the asset never mutates

    ActionRuntime runtime;
    runtime.SetMap(effective);
    runtime.DisableSet(u8"Menu");
    FakeDevices devices;
    const ActionRef jump = runtime.Resolve(u8"Jump");
    devices.keyboard.Set(dshell::KeyCode::Space, true);   // the OLD binding: dead
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(jump));
    devices.keyboard.Set(dshell::KeyCode::J, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.IsDown(jump));

    // Reset-to-default = clear the override; the pristine asset re-applies.
    overlay.Clear(u8"Gameplay", u8"Jump");
    InputMap restored = asset;
    ApplyBindingOverrides(restored, overlay);
    CHECK(restored.sets[0].actions[0].bindings.Size() == 2);

    // The section round-trips through the settings store (user file persistence).
    {
        Array<Binding> replacement;
        Binding k;
        k.source = BindingSource::Key;
        k.code = static_cast<u32>(dshell::KeyCode::K);
        replacement.PushBack(k);
        overlay.Set(u8"Gameplay", u8"Jump", static_cast<Array<Binding>&&>(replacement));
    }
    draconic::settings::Settings store;
    store.Section<InputBindingOverrides>().overrides = overlay.overrides;   // sections are non-copyable objects
    MemoryStream file;
    REQUIRE(store.Save(file, BinarySerializerFactory()).IsOk());
    (void)file.Seek(0, SeekOrigin::Begin);
    draconic::settings::Settings loadedStore;
    REQUIRE(loadedStore.Load(file, BinarySerializerFactory()).IsOk());
    const InputBindingOverrides* loaded = loadedStore.Find<InputBindingOverrides>();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->overrides.Size() == 1);
    CHECK(loaded->overrides[0].bindings[0].code == static_cast<u32>(dshell::KeyCode::K));
}

TEST_CASE("input: the Wren Input facade resolves PER-CONTEXT services")
{
    draconic::input::RegisterInputScriptApi();

    // Two runtimes, two contexts - each script reads ITS OWN bound runtime (players /
    // editor-vs-game). No process globals anywhere.
    ActionRuntime runtimeA;
    runtimeA.SetMap(MakeGameplayMap());
    runtimeA.DisableSet(u8"Menu");
    ActionRuntime runtimeB;
    runtimeB.SetMap(MakeGameplayMap());
    runtimeB.DisableSet(u8"Menu");

    FakeDevices devices;
    devices.keyboard.Set(dshell::KeyCode::Space, true);
    devices.keyboard.Set(dshell::KeyCode::W, true);
    runtimeA.Update(devices, 1.0f / 60.0f);   // A sees the press...
    FakeDevices idle;
    runtimeB.Update(idle, 1.0f / 60.0f);      // ...B sees nothing

    RefPtr<draconic::script::IScriptManager> manager =
        draconic::script::wren::CreateScriptManager();
    draconic::script::RegisterReflectedTypes(*manager);

    RefPtr<draconic::script::IScriptContext> ctxA = manager->CreateContext();
    RefPtr<draconic::script::IScriptContext> ctxB = manager->CreateContext();
    RefPtr<draconic::script::IScriptContext> ctxNone = manager->CreateContext();
    REQUIRE(ctxA.Get() != nullptr);
    ctxA->SetService(draconic::input::kInputRuntimeService, &runtimeA);
    ctxB->SetService(draconic::input::kInputRuntimeService, &runtimeB);

    const StringView script =
        u8"var Down = Input.isDown(\"Jump\")\n"
        u8"var MoveY = Input.valueY(\"Move\")\n";
    REQUIRE(ctxA->Load(script, u8"main").IsOk());
    CHECK(ctxA->GetGlobal(u8"Down").Get<bool>() == true);
    CHECK(ctxA->GetGlobal(u8"MoveY").Get<f64>() == doctest::Approx(1.0));

    REQUIRE(ctxB->Load(script, u8"main").IsOk());
    CHECK(ctxB->GetGlobal(u8"Down").Get<bool>() == false);   // B's runtime saw nothing
    CHECK(ctxB->GetGlobal(u8"MoveY").Get<f64>() == doctest::Approx(0.0));

    // No service bound: released, never a crash.
    REQUIRE(ctxNone->Load(script, u8"main").IsOk());
    CHECK(ctxNone->GetGlobal(u8"Down").Get<bool>() == false);

    // Script-driven exclusive push lands on the CONTEXT's runtime only.
    runtimeA.EnableSet(u8"Menu");
    REQUIRE(ctxA->Load(u8"Input.pushSet(\"Menu\")\n", u8"main").IsOk());
    CHECK(runtimeA.ExclusiveDepth() == 1);
    CHECK(runtimeB.ExclusiveDepth() == 0);
}

TEST_CASE("input: rebind capture - first activated input matching the filter")
{
    FakeDevices devices;
    FakeGamepad pad;
    devices.pads.PushBack(&pad);
    Binding captured;

    // Nothing active: keeps listening.
    CaptureFilter keysOnly;
    keysOnly.mouseButtons = false;
    keysOnly.gamepadButtons = false;
    CHECK_FALSE(CaptureBinding(devices, keysOnly, captured));

    // A key press captures as a Key binding.
    devices.keyboard.pressed[static_cast<u32>(dshell::KeyCode::F)] = true;
    REQUIRE(CaptureBinding(devices, keysOnly, captured));
    CHECK(captured.source == BindingSource::Key);
    CHECK(captured.code == static_cast<u32>(dshell::KeyCode::F));
    devices.keyboard.pressed[static_cast<u32>(dshell::KeyCode::F)] = false;

    // Stick noise is IGNORED for a key rebind; an opted-in stick filter captures it.
    pad.axes[static_cast<u32>(dshell::GamepadAxis::RightX)] = 0.9f;
    CHECK_FALSE(CaptureBinding(devices, keysOnly, captured));
    CaptureFilter stickFilter;
    stickFilter.keys = false;
    stickFilter.mouseButtons = false;
    stickFilter.gamepadButtons = false;
    stickFilter.gamepadSticks = true;
    REQUIRE(CaptureBinding(devices, stickFilter, captured));
    CHECK(captured.source == BindingSource::GamepadStick);
    CHECK(captured.code == static_cast<u32>(StickCode::Right));

    // Gamepad button (must-not-have keys still capturing pads).
    pad.axes[static_cast<u32>(dshell::GamepadAxis::RightX)] = 0.0f;
    pad.buttonsPressed[3] = true;
    CaptureFilter padButtons;
    padButtons.keys = false;
    padButtons.mouseButtons = false;
    REQUIRE(CaptureBinding(devices, padButtons, captured));
    CHECK(captured.source == BindingSource::GamepadButton);
    CHECK(captured.code == 3u);
}

TEST_CASE("input: the timeScale processor scales flagged action VALUES only")
{
    InputMap map;
    ActionSet set;
    set.name = String(u8"S");
    auto addAxis = [&](StringView name, bool scaled) {
        Action axis;
        axis.name = String(name);
        axis.kind = ActionKind::Axis1D;
        Binding key;
        key.source = BindingSource::Key;
        key.code = static_cast<u32>(dshell::KeyCode::W);
        axis.bindings.PushBack(key);
        axis.processors.timeScale = scaled;
        set.actions.PushBack(static_cast<Action&&>(axis));
    };
    addAxis(u8"Scaled", true);
    addAxis(u8"Raw", false);
    map.sets.PushBack(static_cast<ActionSet&&>(set));

    ActionRuntime runtime;
    runtime.SetMap(map);
    FakeDevices devices;
    devices.keyboard.Set(dshell::KeyCode::W, true);
    const ActionRef scaled = runtime.Resolve(u8"Scaled");
    const ActionRef raw = runtime.Resolve(u8"Raw");

    runtime.SetTimeScale(0.25f);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.Value(scaled) == doctest::Approx(0.25f));
    CHECK(runtime.Value(raw) == doctest::Approx(1.0f));
    CHECK(runtime.IsDown(raw));

    runtime.SetTimeScale(0.0f);   // paused world: flagged values zero, digital press intact
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.Value(scaled) == doctest::Approx(0.0f));
    CHECK(runtime.IsDown(scaled));   // the PRESS is physical; only the value scales
}

TEST_CASE("input: interactions - hold, tap, and double tap")
{
    auto makeButtonMap = [](InteractionKind kind, f32 seconds) {
        InputMap map;
        ActionSet set;
        set.name = String(u8"S");
        Action action;
        action.name = String(u8"Act");
        action.kind = ActionKind::Button;
        Binding key;
        key.source = BindingSource::Key;
        key.code = static_cast<u32>(dshell::KeyCode::Space);
        action.bindings.PushBack(key);
        action.interaction.kind = kind;
        action.interaction.seconds = seconds;
        set.actions.PushBack(static_cast<Action&&>(action));
        map.sets.PushBack(static_cast<ActionSet&&>(set));
        return map;
    };
    const f32 step = 1.0f / 60.0f;
    FakeDevices devices;

    // HOLD 0.2s: pressing does nothing until the threshold; then one pressed edge; the
    // release edge fires on release as usual.
    {
        ActionRuntime runtime;
        runtime.SetMap(makeButtonMap(InteractionKind::Hold, 0.2f));
        const ActionRef act = runtime.Resolve(u8"Act");
        devices.keyboard.Set(dshell::KeyCode::Space, true);
        for (int i = 0; i < 6; ++i)
        {
            runtime.Update(devices, step);
            CHECK_FALSE(runtime.IsDown(act));   // 6 frames = 0.1s, below the threshold
        }
        bool edged = false;
        for (int i = 0; i < 8; ++i)
        {
            runtime.Update(devices, step);
            if (runtime.WasPressed(act)) { edged = true; }
        }
        CHECK(edged);
        CHECK(runtime.IsDown(act));
        devices.keyboard.Set(dshell::KeyCode::Space, false);
        runtime.Update(devices, step);
        CHECK_FALSE(runtime.IsDown(act));
        CHECK(runtime.WasReleased(act));
    }

    // TAP 0.15s: a short press pulses ONE frame at release; a long press never fires.
    {
        ActionRuntime runtime;
        runtime.SetMap(makeButtonMap(InteractionKind::Tap, 0.15f));
        const ActionRef act = runtime.Resolve(u8"Act");
        devices.keyboard.Set(dshell::KeyCode::Space, true);
        for (int i = 0; i < 4; ++i) { runtime.Update(devices, step); CHECK_FALSE(runtime.IsDown(act)); }
        devices.keyboard.Set(dshell::KeyCode::Space, false);
        runtime.Update(devices, step);
        CHECK(runtime.WasPressed(act));   // the pulse
        CHECK(runtime.IsDown(act));
        runtime.Update(devices, step);
        CHECK_FALSE(runtime.IsDown(act));
        CHECK(runtime.WasReleased(act));

        devices.keyboard.Set(dshell::KeyCode::Space, true);   // long press: no fire
        for (int i = 0; i < 20; ++i) { runtime.Update(devices, step); }
        devices.keyboard.Set(dshell::KeyCode::Space, false);
        runtime.Update(devices, step);
        CHECK_FALSE(runtime.WasPressed(act));
    }

    // DOUBLE TAP 0.25s: two quick presses pulse on the SECOND; slow presses never fire.
    {
        ActionRuntime runtime;
        runtime.SetMap(makeButtonMap(InteractionKind::DoubleTap, 0.25f));
        const ActionRef act = runtime.Resolve(u8"Act");
        auto tap = [&](int gapFrames) {
            devices.keyboard.Set(dshell::KeyCode::Space, true);
            runtime.Update(devices, step);
            const bool fired = runtime.WasPressed(act);
            devices.keyboard.Set(dshell::KeyCode::Space, false);
            runtime.Update(devices, step);
            for (int i = 0; i < gapFrames; ++i) { runtime.Update(devices, step); }
            return fired;
        };
        CHECK_FALSE(tap(2));   // first tap arms
        CHECK(tap(2));         // second within the window fires
        CHECK_FALSE(tap(30));  // slow: arms again (previous consumed), gap too long...
        CHECK_FALSE(tap(30));  // ...and a second slow tap still does not fire
    }
}

TEST_CASE("input: smoothing - sensitivity ramp, gravity recenter, snap on flip")
{
    InputMap map;
    ActionSet set;
    set.name = String(u8"S");
    Action axis;
    axis.name = String(u8"Throttle");
    axis.kind = ActionKind::Axis1D;
    Binding key;
    key.source = BindingSource::Key;
    key.code = static_cast<u32>(dshell::KeyCode::W);
    axis.bindings.PushBack(key);
    Binding back;
    back.source = BindingSource::Key;
    back.code = static_cast<u32>(dshell::KeyCode::S);
    back.scale = -1.0f;
    axis.bindings.PushBack(back);
    axis.processors.sensitivity = 5.0f;   // 0.2s to full
    axis.processors.gravity = 10.0f;      // 0.1s back to zero
    axis.processors.snap = true;
    set.actions.PushBack(static_cast<Action&&>(axis));
    map.sets.PushBack(static_cast<ActionSet&&>(set));

    ActionRuntime runtime;
    runtime.SetMap(map);
    FakeDevices devices;
    const ActionRef throttle = runtime.Resolve(u8"Throttle");

    // Ramp: after 0.1s at sensitivity 5, value is ~0.5, not 1.
    devices.keyboard.Set(dshell::KeyCode::W, true);
    for (int i = 0; i < 6; ++i) { runtime.Update(devices, 1.0f / 60.0f); }
    CHECK(runtime.Value(throttle) == doctest::Approx(0.5f).epsilon(0.05));
    for (int i = 0; i < 12; ++i) { runtime.Update(devices, 1.0f / 60.0f); }
    CHECK(runtime.Value(throttle) == doctest::Approx(1.0f));

    // Snap: flipping to S zeroes first, then ramps negative (never crossfades through).
    devices.keyboard.Set(dshell::KeyCode::W, false);
    devices.keyboard.Set(dshell::KeyCode::S, true);
    runtime.Update(devices, 1.0f / 60.0f);
    CHECK(runtime.Value(throttle) <= 0.0f);

    // Gravity: releasing recenters at 10/sec - ~0.1s to zero from full.
    devices.keyboard.Set(dshell::KeyCode::S, false);
    for (int i = 0; i < 8; ++i) { runtime.Update(devices, 1.0f / 60.0f); }
    CHECK(runtime.Value(throttle) == doctest::Approx(0.0f));
}
