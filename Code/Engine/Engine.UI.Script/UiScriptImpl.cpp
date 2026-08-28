// Engine::UI.Script - implementation unit: view-handle + `ui` facade bodies, the REFLECT_VALUE /
// REFLECT_MEMBERS registrations, and the RegisterUiScriptSurface entry point. Kept out of the interface
// units (the REFLECT bodies + foundation.ui contact never sit in a module interface - GCC gcm-cluster
// rule).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.ui.script;

import foundation.core;
import foundation.ui;
import foundation.ui.gamekit;
import foundation.script;
import foundation.script.facades; // RegisterExtraFacadeName

using namespace foundation::core;

namespace engine::uiscript
{
    namespace ui = foundation::ui;
    namespace gamekit = foundation::ui::gamekit;

    // ------------------------------------------------------------------- handle helpers (file-local) --
    namespace
    {
        template <typename T>
        [[nodiscard]] T* As(const RefPtr<ui::View>& view)
        {
            return Cast<T>(view.Get());
        }

        // Wrap a borrowed CORE view in a script handle of type H (H.view is an owning RefPtr).
        template <typename H>
        [[nodiscard]] H Wrap(ui::View* v)
        {
            H handle;
            handle.view = (v != nullptr) ? RefPtr<ui::View>(v) : RefPtr<ui::View>{};
            return handle;
        }

        // Find the first descendant named `name` and cast to CoreT; wrap in handle H (null on miss or
        // type mismatch - loud-null).
        template <typename CoreT, typename H>
        [[nodiscard]] H FindAs(const RefPtr<ui::View>& group, StringView name)
        {
            if (auto* g = Cast<ui::ViewGroup>(group.Get()))
            {
                return Wrap<H>(g->template FindByName<CoreT>(name));
            }
            return H{};
        }

        RefPtr<gamekit::UIScreen> MakeScreenFromDocument(UiScreenScriptBinding* b,
                                                         const Guid& document)
        {
            if (b == nullptr || !b->instantiate || document.IsNil())
            {
                return {};
            }
            RefPtr<ui::View> tree = b->instantiate(document);
            if (!tree)
            {
                return {};
            }
            if (auto* s = Cast<gamekit::UIScreen>(tree.Get()))
            {
                return RefPtr<gamekit::UIScreen>(s); // the document root IS a <screen>
            }
            RefPtr<gamekit::UIScreen> wrapper = MakeRef<gamekit::UIScreen>(DefaultAllocator());
            wrapper->AddView(tree.Get()); // wrap a plain root in a default (Modal) screen
            return wrapper;
        }
    }

    // ============================================================================ common handle ops ===
    // Defined once via a macro (the value structs share the members but have no inheritance).
#define UI_SCRIPT_DEFINE_COMMON(H)                                                                      \
    bool H::isValid() const { return static_cast<bool>(view); }                                         \
    String H::name() const { return view ? String(view->Name.AsView()) : String{}; }                   \
    bool H::visible() const { return view && view->Visibility == ui::Visibility::Visible; }             \
    bool H::enabled() const { return view && view->IsEnabled; }                                         \
    void H::setVisible(bool value)                                                                      \
    {                                                                                                   \
        if (view)                                                                                       \
        {                                                                                               \
            view->Visibility = value ? ui::Visibility::Visible : ui::Visibility::Hidden;                \
        }                                                                                               \
    }                                                                                                   \
    void H::setEnabled(bool value)                                                                      \
    {                                                                                                   \
        if (view)                                                                                       \
        {                                                                                               \
            view->IsEnabled = value;                                                                    \
        }                                                                                               \
    }

    UI_SCRIPT_DEFINE_COMMON(View)
    UI_SCRIPT_DEFINE_COMMON(Label)
    UI_SCRIPT_DEFINE_COMMON(Button)
    UI_SCRIPT_DEFINE_COMMON(ProgressBar)
    UI_SCRIPT_DEFINE_COMMON(TextBox)
    UI_SCRIPT_DEFINE_COMMON(ViewGroup)
    UI_SCRIPT_DEFINE_COMMON(Screen)
#undef UI_SCRIPT_DEFINE_COMMON

    // ------------------------------------------------------------------------------- Label / Button ---
    String Label::text() const
    {
        auto* l = As<ui::Label>(view);
        return (l != nullptr) ? String(l->Text.Value().AsView()) : String{};
    }
    void Label::setText(String value)
    {
        if (auto* l = As<ui::Label>(view))
        {
            l->Text.SetValue(Move(value));
        }
    }

    String Button::text() const
    {
        auto* b = As<ui::Button>(view);
        return (b != nullptr) ? String(b->Text.Value().AsView()) : String{};
    }
    void Button::setText(String value)
    {
        if (auto* b = As<ui::Button>(view))
        {
            b->Text.SetValue(Move(value));
        }
    }
    void Button::onClick(RefPtr<foundation::script::IScriptDelegate> handler)
    {
        auto* b = As<ui::Button>(view);
        if (b == nullptr || !handler)
        {
            return; // null-but-valid handle, or no handler: a safe no-op (loud-null idiom).
        }
        // Capturing the delegate RefPtr keeps the script function alive for exactly the lifetime of this
        // button's OnClick subscription (released when the screen - and button - is destroyed).
        //
        // A click is dispatched while the view tree is being traversed, and the handler is ARBITRARY
        // script: it may pop/push a screen, but also despawn an entity, tear down UI, etc. - ANY
        // structural mutation, none of which may run re-entrantly during that traversal. Input is
        // dispatched at the Idle phase (the InputManager sets no phase of its own), so a phase check
        // would NOT catch this. So the handler NEVER runs inline: it goes through the UIContext mutation
        // queue and runs at the next frame's drain - a quiescent point where every structural op is safe
        // (the same queue the ScreenStack and dialogs use). With no context (a headless / unattached
        // button) nothing is dispatching, so running directly is safe.
        b->OnClick.Add(
            [handler](ui::ButtonBase* btn)
            {
                ui::UIContext* ctx = (btn != nullptr) ? btn->Context : nullptr;
                if (ctx != nullptr)
                {
                    ctx->MutationQueueRef().QueueAction(
                        [handler] { (void)handler->Invoke(Span<Variant>{}); });
                }
                else
                {
                    (void)handler->Invoke(Span<Variant>{});
                }
            });
    }

    // --------------------------------------------------------------------------------- ProgressBar ---
    f64 ProgressBar::value() const
    {
        auto* p = As<ui::ProgressBar>(view);
        return (p != nullptr) ? static_cast<f64>(p->Value.Value()) : 0.0;
    }
    void ProgressBar::setValue(f64 value)
    {
        if (auto* p = As<ui::ProgressBar>(view))
        {
            p->Value.SetValue(static_cast<f32>(value));
        }
    }

    // ------------------------------------------------------------------------------------- TextBox ---
    String TextBox::text() const
    {
        auto* e = As<ui::EditText>(view);
        return (e != nullptr) ? String(e->Text()) : String{};
    }
    void TextBox::setText(String value)
    {
        if (auto* e = As<ui::EditText>(view))
        {
            e->SetText(value.AsView());
        }
    }

    // --------------------------------------------------------- ViewGroup / Screen finders (shared) ---
#define UI_SCRIPT_DEFINE_FINDERS(H)                                                                     \
    i32 H::childCount() const                                                                           \
    {                                                                                                   \
        auto* g = As<ui::ViewGroup>(view);                                                              \
        return (g != nullptr) ? static_cast<i32>(g->ChildCount()) : 0;                                  \
    }                                                                                                   \
    View H::childAt(i32 index) const                                                                    \
    {                                                                                                   \
        auto* g = As<ui::ViewGroup>(view);                                                              \
        if (g == nullptr || index < 0 || index >= static_cast<i32>(g->ChildCount()))                    \
        {                                                                                               \
            return View{};                                                                              \
        }                                                                                               \
        return Wrap<View>(g->GetChildAt(static_cast<usize>(index)));                                    \
    }                                                                                                   \
    View H::findByName(String name) const { return FindAs<ui::View, View>(view, name.AsView()); }       \
    Label H::findLabel(String name) const { return FindAs<ui::Label, Label>(view, name.AsView()); }     \
    Button H::findButton(String name) const                                                             \
    {                                                                                                   \
        return FindAs<ui::Button, Button>(view, name.AsView());                                         \
    }                                                                                                   \
    ProgressBar H::findProgressBar(String name) const                                                   \
    {                                                                                                   \
        return FindAs<ui::ProgressBar, ProgressBar>(view, name.AsView());                               \
    }                                                                                                   \
    TextBox H::findTextBox(String name) const                                                           \
    {                                                                                                   \
        return FindAs<ui::EditText, TextBox>(view, name.AsView());                                      \
    }                                                                                                   \
    ViewGroup H::findGroup(String name) const                                                           \
    {                                                                                                   \
        return FindAs<ui::ViewGroup, ViewGroup>(view, name.AsView());                                   \
    }

    UI_SCRIPT_DEFINE_FINDERS(ViewGroup)
    UI_SCRIPT_DEFINE_FINDERS(Screen)
#undef UI_SCRIPT_DEFINE_FINDERS

    // findScreen lives on ViewGroup only (a screen inside a screen is not a v1 shape).
    Screen ViewGroup::findScreen(String name) const
    {
        return FindAs<gamekit::UIScreen, Screen>(view, name.AsView());
    }

    // ================================================================================= `ui` facade ===
    UiScreenScriptBinding* Ui::Resolve()
    {
        foundation::script::IScriptContext* context = foundation::script::CurrentScriptContext();
        return (context != nullptr)
                   ? static_cast<UiScreenScriptBinding*>(context->GetService(kUiScreenScriptService))
                   : nullptr;
    }

    ViewGroup Ui::root()
    {
        UiScreenScriptBinding* b = Resolve();
        if (b == nullptr)
        {
            return Wrap<ViewGroup>(nullptr);
        }
        // The ScreenStack's attached root is the SOURCE OF TRUTH for where pushed screens live
        // (Push adds to it). The separately-captured screenRoot can diverge - in an embedded host it
        // was captured stale/null while the stack still pointed at the live root - which made
        // ui::findLabel() miss a pushed HUD even though ui::top() found it. Prefer the stack's root;
        // fall back to screenRoot only when no stack is wired.
        foundation::ui::RootView* root =
            (b->stack != nullptr && b->stack->Root() != nullptr) ? b->stack->Root() : b->screenRoot;
        return Wrap<ViewGroup>(root);
    }
    Screen Ui::top()
    {
        UiScreenScriptBinding* b = Resolve();
        return Wrap<Screen>((b != nullptr && b->stack != nullptr) ? b->stack->Top() : nullptr);
    }
    i32 Ui::count()
    {
        UiScreenScriptBinding* b = Resolve();
        return (b != nullptr && b->stack != nullptr) ? static_cast<i32>(b->stack->Count()) : 0;
    }

    View Ui::find(String name) { return root().findByName(Move(name)); }
    Label Ui::findLabel(String name) { return root().findLabel(Move(name)); }
    Button Ui::findButton(String name) { return root().findButton(Move(name)); }
    ProgressBar Ui::findProgressBar(String name) { return root().findProgressBar(Move(name)); }
    TextBox Ui::findTextBox(String name) { return root().findTextBox(Move(name)); }
    ViewGroup Ui::findGroup(String name) { return root().findGroup(Move(name)); }

    Screen Ui::push(Guid document)
    {
        UiScreenScriptBinding* b = Resolve();
        if (b == nullptr || b->stack == nullptr)
        {
            return Screen{};
        }
        RefPtr<gamekit::UIScreen> screen = MakeScreenFromDocument(b, document);
        if (!screen)
        {
            return Screen{};
        }
        return Wrap<Screen>(b->stack->Push(screen));
    }
    void Ui::pop()
    {
        UiScreenScriptBinding* b = Resolve();
        if (b != nullptr && b->stack != nullptr)
        {
            b->stack->Pop();
        }
    }
    Screen Ui::replace(Guid document)
    {
        UiScreenScriptBinding* b = Resolve();
        if (b == nullptr || b->stack == nullptr)
        {
            return Screen{};
        }
        RefPtr<gamekit::UIScreen> screen = MakeScreenFromDocument(b, document);
        if (!screen)
        {
            return Screen{};
        }
        return Wrap<Screen>(b->stack->Replace(screen));
    }
    void Ui::clear()
    {
        UiScreenScriptBinding* b = Resolve();
        if (b != nullptr && b->stack != nullptr)
        {
            b->stack->Clear();
        }
    }
    bool Ui::back()
    {
        UiScreenScriptBinding* b = Resolve();
        return (b != nullptr && b->stack != nullptr) ? b->stack->HandleBack() : false;
    }

    // ================================================================================== reflection ===
    REFLECT_VALUE(View, "rtti::engine.ui.script")
    {
        builder.Method<&View::isValid>("isValid");
        builder.ComputedProperty<&View::name>("name");
        builder.ComputedProperty<&View::visible>("visible");
        builder.ComputedProperty<&View::enabled>("enabled");
        builder.Method<&View::setVisible>("setVisible", {"value"});
        builder.Method<&View::setEnabled>("setEnabled", {"value"});
        builder.Constructor();
    }
    REFLECT_VALUE(Label, "rtti::engine.ui.script")
    {
        builder.Method<&Label::isValid>("isValid");
        builder.ComputedProperty<&Label::name>("name");
        builder.ComputedProperty<&Label::visible>("visible");
        builder.ComputedProperty<&Label::enabled>("enabled");
        builder.ComputedProperty<&Label::text>("text");
        builder.Method<&Label::setVisible>("setVisible", {"value"});
        builder.Method<&Label::setEnabled>("setEnabled", {"value"});
        builder.Method<&Label::setText>("setText", {"value"});
        builder.Constructor();
    }
    REFLECT_VALUE(Button, "rtti::engine.ui.script")
    {
        builder.Method<&Button::isValid>("isValid");
        builder.ComputedProperty<&Button::name>("name");
        builder.ComputedProperty<&Button::visible>("visible");
        builder.ComputedProperty<&Button::enabled>("enabled");
        builder.ComputedProperty<&Button::text>("text");
        builder.Method<&Button::setVisible>("setVisible", {"value"});
        builder.Method<&Button::setEnabled>("setEnabled", {"value"});
        builder.Method<&Button::setText>("setText", {"value"});
        builder.Method<&Button::onClick>("onClick", {"handler"});
        builder.Constructor();
    }
    REFLECT_VALUE(ProgressBar, "rtti::engine.ui.script")
    {
        builder.Method<&ProgressBar::isValid>("isValid");
        builder.ComputedProperty<&ProgressBar::name>("name");
        builder.ComputedProperty<&ProgressBar::visible>("visible");
        builder.ComputedProperty<&ProgressBar::enabled>("enabled");
        builder.ComputedProperty<&ProgressBar::value>("value");
        builder.Method<&ProgressBar::setVisible>("setVisible", {"value"});
        builder.Method<&ProgressBar::setEnabled>("setEnabled", {"value"});
        builder.Method<&ProgressBar::setValue>("setValue", {"value"});
        builder.Constructor();
    }
    REFLECT_VALUE(TextBox, "rtti::engine.ui.script")
    {
        builder.Method<&TextBox::isValid>("isValid");
        builder.ComputedProperty<&TextBox::name>("name");
        builder.ComputedProperty<&TextBox::visible>("visible");
        builder.ComputedProperty<&TextBox::enabled>("enabled");
        builder.ComputedProperty<&TextBox::text>("text");
        builder.Method<&TextBox::setVisible>("setVisible", {"value"});
        builder.Method<&TextBox::setEnabled>("setEnabled", {"value"});
        builder.Method<&TextBox::setText>("setText", {"value"});
        builder.Constructor();
    }
    REFLECT_VALUE(ViewGroup, "rtti::engine.ui.script")
    {
        builder.Method<&ViewGroup::isValid>("isValid");
        builder.ComputedProperty<&ViewGroup::name>("name");
        builder.ComputedProperty<&ViewGroup::visible>("visible");
        builder.ComputedProperty<&ViewGroup::enabled>("enabled");
        builder.ComputedProperty<&ViewGroup::childCount>("childCount");
        builder.Method<&ViewGroup::setVisible>("setVisible", {"value"});
        builder.Method<&ViewGroup::setEnabled>("setEnabled", {"value"});
        builder.Method<&ViewGroup::childAt>("childAt", {"index"});
        builder.Method<&ViewGroup::findByName>("findByName", {"name"});
        builder.Method<&ViewGroup::findLabel>("findLabel", {"name"});
        builder.Method<&ViewGroup::findButton>("findButton", {"name"});
        builder.Method<&ViewGroup::findProgressBar>("findProgressBar", {"name"});
        builder.Method<&ViewGroup::findTextBox>("findTextBox", {"name"});
        builder.Method<&ViewGroup::findGroup>("findGroup", {"name"});
        builder.Method<&ViewGroup::findScreen>("findScreen", {"name"});
        builder.Constructor();
    }
    REFLECT_VALUE(Screen, "rtti::engine.ui.script")
    {
        builder.Method<&Screen::isValid>("isValid");
        builder.ComputedProperty<&Screen::name>("name");
        builder.ComputedProperty<&Screen::visible>("visible");
        builder.ComputedProperty<&Screen::enabled>("enabled");
        builder.ComputedProperty<&Screen::childCount>("childCount");
        builder.Method<&Screen::setVisible>("setVisible", {"value"});
        builder.Method<&Screen::setEnabled>("setEnabled", {"value"});
        builder.Method<&Screen::childAt>("childAt", {"index"});
        builder.Method<&Screen::findByName>("findByName", {"name"});
        builder.Method<&Screen::findLabel>("findLabel", {"name"});
        builder.Method<&Screen::findButton>("findButton", {"name"});
        builder.Method<&Screen::findProgressBar>("findProgressBar", {"name"});
        builder.Method<&Screen::findTextBox>("findTextBox", {"name"});
        builder.Method<&Screen::findGroup>("findGroup", {"name"});
        builder.Constructor();
    }

    REFLECT_MEMBERS(Ui, "rtti::engine.ui.script")
    {
        builder.Attribute("scriptName", "ui"); // reserved lowercase `ui` (ScriptName alias)
        builder.Method<&Ui::root>("root");
        builder.Method<&Ui::top>("top");
        builder.Method<&Ui::count>("count");
        builder.Method<&Ui::find>("find", {"name"});
        builder.Method<&Ui::findLabel>("findLabel", {"name"});
        builder.Method<&Ui::findButton>("findButton", {"name"});
        builder.Method<&Ui::findProgressBar>("findProgressBar", {"name"});
        builder.Method<&Ui::findTextBox>("findTextBox", {"name"});
        builder.Method<&Ui::findGroup>("findGroup", {"name"});
        builder.Method<&Ui::push>("push", {"document"});
        builder.Method<&Ui::pop>("pop");
        builder.Method<&Ui::replace>("replace", {"document"});
        builder.Method<&Ui::clear>("clear");
        builder.Method<&Ui::back>("back");
        builder.Constructor();
    }

    void RegisterUiScriptTypes()
    {
        static const bool once = []()
        {
            RttiRegisterValue_View();
            RttiRegisterValue_Label();
            RttiRegisterValue_Button();
            RttiRegisterValue_ProgressBar();
            RttiRegisterValue_TextBox();
            RttiRegisterValue_ViewGroup();
            RttiRegisterValue_Screen();
            GlobalTypeRegistry().Register(TypeOf<View>());
            GlobalTypeRegistry().Register(TypeOf<Label>());
            GlobalTypeRegistry().Register(TypeOf<Button>());
            GlobalTypeRegistry().Register(TypeOf<ProgressBar>());
            GlobalTypeRegistry().Register(TypeOf<TextBox>());
            GlobalTypeRegistry().Register(TypeOf<ViewGroup>());
            GlobalTypeRegistry().Register(TypeOf<Screen>());
            return true;
        }();
        (void)once;
    }

    void RegisterUiScriptSurface()
    {
        static const bool once = []()
        {
            RegisterUiScriptTypes();                           // finders return these value handles
            GlobalTypeRegistry().Register(Ui::StaticType());   // binds as `ui` (its scriptName alias)
            foundation::script::RegisterExtraFacadeName(u8"ui"); // prelude imports the ALIAS, not Ui
            return true;
        }();
        (void)once;
    }
}
