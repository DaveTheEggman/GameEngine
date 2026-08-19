// engine.ui.script - the `ui` script facade PROVEN end-to-end on both backends (game-ui-kit P1,
// replacing the old id-addressed Ui facade + its parity suite).
//
// This drives a real AngelScript / Luau VM against a real screen tier: a UIContext + RootView + a
// gamekit ScreenStack, with an `instantiate` that returns a document tree carrying named controls
// (a Label "status", a ProgressBar "progress", a Button "cancel"). A script pushes the document,
// finds controls by name + type, writes + reads them back, checks a type-mismatch loud-null, and pops.
// We read the round-tripped values (proving writes reached the REAL views) and the live stack count.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;         // UIContext / RootView / Label / ProgressBar / Button / FrameLayout
import foundation.ui.gamekit; // ScreenStack
import engine.ui.script;      // the `ui` facade + UiScreenScriptBinding + InstallUiScreenScriptService
import foundation.script;
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif

using namespace foundation::core;
using namespace foundation::script;

namespace
{
    namespace ui = foundation::ui;
    namespace gamekit = foundation::ui::gamekit;

    // A real screen tier: context + root + stack, with an instantiate that returns a fresh document
    // tree of named controls (the facade wraps a plain root in a default UIScreen on push).
    struct UiBed
    {
        ui::UIContext context;
        RefPtr<ui::RootView> root;
        gamekit::ScreenStack stack;
        engine::uiscript::UiScreenScriptBinding binding;

        UiBed()
        {
            root = MakeRef<ui::RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            stack.Attach(root.Get());
            binding.screenRoot = root.Get();
            binding.stack = &stack;
            binding.instantiate = Function<RefPtr<ui::View>(const Guid&)>{
                [](const Guid&) -> RefPtr<ui::View>
                {
                    auto group = MakeRef<ui::FrameLayout>(DefaultAllocator());
                    auto label = MakeRef<ui::Label>(DefaultAllocator());
                    label->Name = String(u8"status");
                    group->AddView(label.Get());
                    auto bar = MakeRef<ui::ProgressBar>(DefaultAllocator());
                    bar->Name = String(u8"progress");
                    group->AddView(bar.Get());
                    auto btn = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Cancel"));
                    btn->Name = String(u8"cancel");
                    group->AddView(btn.Get());
                    return group;
                }};
        }

        // The live controls, found through the screen root (null once the screen is popped).
        [[nodiscard]] ui::Label* StatusLabel() const
        {
            return root->FindByName<ui::Label>(u8"status");
        }
    };

    // Assert the outcome of any backend's run of the shared script. Kept to backend-NEUTRAL types
    // (String + bool globals; the numeric stack count is checked on the C++ side to dodge the
    // AngelScript-i32 / Luau-f64 number-type split).
    void CheckOutcome(const UiBed& bed, IScriptContext& ctx)
    {
        // Writes reached the REAL views: the in-script round-trip read "Loading" back off the label.
        CHECK(ctx.GetGlobal(u8"t").template Get<String>() == StringView(u8"Loading"));
        // Loud-null: "status" is a Label, so findButton("status") is a null-but-valid handle.
        CHECK(ctx.GetGlobal(u8"wrong").template Get<bool>() == false);
        // The pop landed (C++ side): the stack is empty and the popped screen's controls are gone.
        CHECK(bed.stack.Count() == 0);
        CHECK(bed.StatusLabel() == nullptr);
    }
}

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("ui-facade: AngelScript pushes a screen, finds + drives typed controls, reads back, pops")
{
    RegisterCoreTypes();
    engine::uiscript::RegisterUiScriptSurface();

    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    UiBed bed;
    engine::uiscript::InstallUiScreenScriptService(*ctx, bed.binding);

    // AngelScript: the `ui` facade's statics live in its namespace (ui::push); finders return handles
    // the call chains onto, exactly like the component `.of(...)` surface.
    const Status status =
        ctx->Load(u8"string t; bool wrong;\n"
                  u8"void main() {\n"
                  u8"  ui::push(Guid(17, 34));\n"
                  u8"  ui::findLabel(\"status\").setText(\"Loading\");\n"
                  u8"  ui::findProgressBar(\"progress\").setValue(0.5);\n"
                  u8"  t = ui::findLabel(\"status\").text;\n"
                  u8"  wrong = ui::findButton(\"status\").isValid();\n"
                  u8"  ui::pop();\n"
                  u8"}\n",
                  u8"main");
    REQUIRE(status.IsOk());
    CheckOutcome(bed, *ctx);
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("ui-facade: Luau pushes a screen, finds + drives typed controls, reads back, pops")
{
    RegisterCoreTypes();
    engine::uiscript::RegisterUiScriptSurface();

    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    UiBed bed;
    engine::uiscript::InstallUiScreenScriptService(*ctx, bed.binding);

    // Luau: method calls use `:` and properties read with `.`, but the surface is otherwise identical
    // to AngelScript - the backend-neutrality proof.
    const Status status =
        ctx->Load(u8"ui.push(Guid.new(17, 34))\n"
                  u8"ui.findLabel(\"status\"):setText(\"Loading\")\n"
                  u8"ui.findProgressBar(\"progress\"):setValue(0.5)\n"
                  u8"t = ui.findLabel(\"status\").text\n"
                  u8"wrong = ui.findButton(\"status\"):isValid()\n"
                  u8"ui.pop()\n",
                  u8"main");
    REQUIRE(status.IsOk());
    CheckOutcome(bed, *ctx);
}
#endif // OPTION_HAS_LUAU
