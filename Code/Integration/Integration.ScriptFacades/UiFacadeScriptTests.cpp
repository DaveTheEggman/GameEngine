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

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("ui-facade: AngelScript binds a button click to a script delegate; firing runs the handler")
{
    RegisterCoreTypes();
    engine::uiscript::RegisterUiScriptSurface();

    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    UiBed bed;
    engine::uiscript::InstallUiScreenScriptService(*ctx, bed.binding);

    // Bind the "cancel" button's click to a handler that does a STRUCTURAL mutation - it pops its own
    // screen - so we prove the handler both defers and can safely restructure. A void no-arg handler is
    // wrapped in the engine's `void Action()` funcdef (ScriptDelegate is double(double)).
    const Status status =
        ctx->Load(u8"bool clicked = false;\n"
                  u8"void onCancel() { clicked = true; ui::pop(); }\n"
                  u8"void main() {\n"
                  u8"  ui::push(Guid(17, 34));\n"
                  u8"  ui::findButton(\"cancel\").onClick(Action(onCancel));\n"
                  u8"}\n",
                  u8"main");
    REQUIRE(status.IsOk());
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == false);
    CHECK(bed.stack.Count() == 1); // screen is up

    // Fire the REAL button's OnClick from C++. The handler does NOT run inline (a click dispatches while
    // the tree is live) - it defers through the mutation queue, so nothing has changed yet.
    ui::Button* cancel = bed.root->FindByName<ui::Button>(u8"cancel");
    REQUIRE(cancel != nullptr);
    cancel->OnClick.Invoke(cancel);
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == false); // deferred, not inline
    CHECK(bed.stack.Count() == 1);

    // Draining next frame runs the handler at a quiescent point, where its structural pop is safe.
    bed.context.MutationQueueRef().Drain();
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == true);
    CHECK(bed.stack.Count() == 0); // the handler's ui::pop() took effect
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("ui-facade: Luau binds a button click to a script function; firing runs the handler")
{
    RegisterCoreTypes();
    engine::uiscript::RegisterUiScriptSurface();

    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    UiBed bed;
    engine::uiscript::InstallUiScreenScriptService(*ctx, bed.binding);

    // Luau passes the function value directly (no ScriptDelegate wrapper); method call uses `:`. The
    // handler pops its own screen - a structural mutation - to prove the deferred path is safe.
    const Status status =
        ctx->Load(u8"clicked = false\n"
                  u8"ui.push(Guid.new(17, 34))\n"
                  u8"ui.findButton(\"cancel\"):onClick(function() clicked = true; ui.pop() end)\n",
                  u8"main");
    REQUIRE(status.IsOk());
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == false);
    CHECK(bed.stack.Count() == 1);

    ui::Button* cancel = bed.root->FindByName<ui::Button>(u8"cancel");
    REQUIRE(cancel != nullptr);
    cancel->OnClick.Invoke(cancel);
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == false); // deferred, not inline
    CHECK(bed.stack.Count() == 1);

    bed.context.MutationQueueRef().Drain();
    CHECK(ctx->GetGlobal(u8"clicked").template Get<bool>() == true);
    CHECK(bed.stack.Count() == 0); // the handler's ui.pop() took effect
}
#endif // OPTION_HAS_LUAU
