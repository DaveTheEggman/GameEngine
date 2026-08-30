// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - ButtonPrompt behavior (native, backend-neutral).
//
// A ButtonPrompt shows "[<binding>] <text>". These cover the raw Set() and SetFromAction() resolving
// an action's first binding through foundation.input's DescribeBinding, plus the unbound fallback.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.shell;
import foundation.ui;
import foundation.ui.gamekit;
import foundation.input;

using namespace foundation::core;
using namespace foundation::ui;
using namespace foundation::ui::gamekit;
namespace input = foundation::input;
namespace shell = foundation::shell;

namespace
{
    struct Bed
    {
        UIContext context;
        RefPtr<RootView> root;
        RefPtr<ButtonPrompt> prompt;
        Bed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            prompt = MakeRef<ButtonPrompt>(DefaultAllocator());
            root->AddView(prompt.Get());
        }
    };

    input::InputMap MapWith(StringView action, input::BindingSource source, u32 code)
    {
        input::InputMap map;
        input::ActionSet set;
        set.name = String(u8"Gameplay");
        input::Action a;
        a.name = String(action);
        input::Binding b;
        b.source = source;
        b.code = code;
        a.bindings.PushBack(b);
        set.actions.PushBack(Move(a));
        map.sets.PushBack(Move(set));
        return map;
    }
}

TEST_CASE("button prompt: Set shows [label] + text")
{
    Bed bed;
    bed.prompt->Set(u8"E", u8"Deliver");
    CHECK(bed.prompt->Keycap()->Text.Value() == StringView(u8"[E]"));
    CHECK(bed.prompt->TextLabel()->Text.Value() == StringView(u8"Deliver"));
}

TEST_CASE("button prompt: SetFromAction resolves the action's first binding (keyboard)")
{
    Bed bed;
    const input::InputMap map =
        MapWith(u8"Deliver", input::BindingSource::Key, static_cast<u32>(shell::KeyCode::E));
    bed.prompt->SetFromAction(map, u8"Deliver", u8"Deliver");
    CHECK(bed.prompt->Keycap()->Text.Value() == StringView(u8"[E]"));
    CHECK(bed.prompt->TextLabel()->Text.Value() == StringView(u8"Deliver"));
}

TEST_CASE("button prompt: SetFromAction resolves a gamepad binding")
{
    Bed bed;
    const input::InputMap map = MapWith(u8"Jump", input::BindingSource::GamepadButton,
                                        static_cast<u32>(shell::GamepadButton::South));
    bed.prompt->SetFromAction(map, u8"Jump", u8"Jump");
    CHECK(bed.prompt->Keycap()->Text.Value() == StringView(u8"[Pad South]"));
}

TEST_CASE("button prompt: SetFromAction on a missing/unbound action shows [-]")
{
    Bed bed;
    const input::InputMap empty;
    bed.prompt->SetFromAction(empty, u8"Nope", u8"Nope");
    CHECK(bed.prompt->Keycap()->Text.Value() == StringView(u8"[-]"));
    CHECK(bed.prompt->TextLabel()->Text.Value() == StringView(u8"Nope"));
}
