// Draconic::EditorScene - the `draconic.editor.scene` module.
//
// The scene subsystem's editor plugin (design doc §3.1): SceneEditorPage (per-page live Scene +
// ViewportView through the real renderer + EditorCamera), its page factory, and the
// RegisterSceneEditor entry point the editor EXECUTABLE calls - the editor core/app never link
// this module.

export module draconic.editor.scene;

export import :camera;
export import :edit;
export import :gizmo;
export import :component_gizmos;
export import :hierarchy;
export import :inspector;
export import :page;
export import :game_page;
export import :material_page;
export import :mesh_page;
export import :particle_effect_page;
export import :model_prefab;
