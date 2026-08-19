// Engine::UI.Script - :types partition
//
// The reflected typed VIEW HANDLES scripts get from the `ui` facade. Each is a copyable value struct
// holding an OWNING RefPtr to a CORE foundation.ui view (Objects are ref-counted; holding a handle keeps
// its subtree alive, so ops on a handle to a popped/detached screen are safe no-ops rather than a UAF).
// Reflected (REFLECT_VALUE, bodies in UiScriptImpl.cpp) so AngelScript/Luau bind them identically.
//
// Reads are parens-less COMPUTED PROPERTIES (`label.text`, `bar.value`); writes are METHODS
// (`label.setText(...)`, `bar.setValue(...)`) - the established facade idiom (the reflection has no
// computed read-write property). Typed finders return null-but-valid handles on a missing name OR a
// type mismatch (loud-null, not a silent wrong-type op).

module;
#include "Core/Prelude.h"

export module engine.ui.script:types;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace engine::uiscript
{
    struct View;
    struct Label;
    struct Button;
    struct ProgressBar;
    struct TextBox;
    struct ViewGroup;
    struct Screen;

    // Common accessors shared (by hand, value structs have no inheritance) across every handle.
#define UI_SCRIPT_COMMON_HANDLE_MEMBERS                                                                 \
    RefPtr<foundation::ui::View> view;                                                                  \
    [[nodiscard]] bool isValid() const;                                                                 \
    [[nodiscard]] String name() const;                                                                  \
    [[nodiscard]] bool visible() const;                                                                 \
    [[nodiscard]] bool enabled() const;                                                                 \
    void setVisible(bool value);                                                                        \
    void setEnabled(bool value);

    /// A bare view - identity + visibility/enabled. What findByName / childAt return.
    struct View
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
    };

    /// A text label. `label.text` reads, `label.setText(...)` writes.
    struct Label
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] String text() const;
        void setText(String value);
    };

    /// A button. `button.text` reads its caption (click handling stays with authored actions in v1).
    struct Button
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] String text() const;
        void setText(String value);
    };

    /// A progress/fill bar. `bar.value` is 0..1.
    struct ProgressBar
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] f64 value() const;
        void setValue(f64 value);
    };

    /// A text input. `box.text` reads/writes the edited text.
    struct TextBox
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] String text() const;
        void setText(String value);
    };

    /// A container - the search surface. Every finder searches this group's subtree recursively, first
    /// match, and returns a null-but-valid handle when the name is missing or is the wrong control type.
    struct ViewGroup
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] i32 childCount() const;
        [[nodiscard]] View childAt(i32 index) const;
        [[nodiscard]] View findByName(String name) const;
        [[nodiscard]] Label findLabel(String name) const;
        [[nodiscard]] Button findButton(String name) const;
        [[nodiscard]] ProgressBar findProgressBar(String name) const;
        [[nodiscard]] TextBox findTextBox(String name) const;
        [[nodiscard]] ViewGroup findGroup(String name) const;
        [[nodiscard]] Screen findScreen(String name) const;
    };

    /// A screen (a gamekit UIScreen) - a container with the same finder surface as ViewGroup, scoped to
    /// one screen so name lookups can be narrowed (`ui.top().findLabel(...)`).
    struct Screen
    {
        UI_SCRIPT_COMMON_HANDLE_MEMBERS
        [[nodiscard]] i32 childCount() const;
        [[nodiscard]] View childAt(i32 index) const;
        [[nodiscard]] View findByName(String name) const;
        [[nodiscard]] Label findLabel(String name) const;
        [[nodiscard]] Button findButton(String name) const;
        [[nodiscard]] ProgressBar findProgressBar(String name) const;
        [[nodiscard]] TextBox findTextBox(String name) const;
        [[nodiscard]] ViewGroup findGroup(String name) const;
    };

#undef UI_SCRIPT_COMMON_HANDLE_MEMBERS

    // Register the reflected view-handle value types with the global registry. Called by
    // RegisterUiScriptSurface; idempotent.
    void RegisterUiScriptTypes();
}
