// Draconic::EditorCore - the `draconic.editor.core` module.
//
// The HEADLESS editor domain layer (docs/design/editor.md §3.1): EditorContext + registries,
// the IEditorCommand/EditorCommandStack undo spine, the EditorPage document model, the
// Selection sets, and the EditorProject (manifest + source/cooked content databases). No UI
// imports - fully unit-testable; the UI shell lives in draconic.editor.app, and per-subsystem
// editor plugins in draconic.<sys>.editor modules register into the context.

export module draconic.editor.core;

export import :command;
export import :selection;
export import :project;
export import :project_registry;
export import :editor_settings;
export import :project_manager;
export import :page;
export import :context;
export import :cook_service;
export import :importer;
export import :log_buffer;
export import :job_service;
export import :export_preset;
export import :export_roots;
export import :export_template;
export import :export_controller;
export import :export_pipeline;
