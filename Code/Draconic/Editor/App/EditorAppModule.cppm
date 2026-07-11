// Draconic::EditorApp - the `draconic.editor.app` module.
//
// The editor UI shell on draconic.ui (docs/design/editor.md §3.1/§3.2): the EditorShell chrome
// (MenuBar / DockManager / StatusBar + the five standard panels), per-user dock-layout
// persistence, and EditorApplication (the runtime IApplication assembling UIHost +
// RuntimeDockableWindowHost + EditorContext/EditorProject). Concrete pages/panels arrive in
// later phases; per-subsystem editor plugins register into the context from the executable.

export module draconic.editor.app;

export import :layout;
export import :log_view;
export import :ui_page;
export import :shell;
export import :application;
