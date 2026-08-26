// Editor::Scene - :view_settings partition.
//
// PER-SCENE view state (not scene DATA): the scene page is the unit of editing context in the
// multi-scene model, so everything a page toggles is per-page - and its persistence is per-scene
// too. Kept in the per-project editor settings store (EditorContext::ProjectEditorSettings()) keyed
// by the scene's guid, mirroring the mesh/material preview prefs. Grid on/off for now; future
// page-scoped viewport toggles (gizmo visibility, snap, ...) become more fields on SceneViewPref.
//
// Lives in its own light partition (not the heavy ScenePage.cppm interface) so the RTTI body stays
// out of that interface's gcm and the section is nameable from tests.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.scene:view_settings;

import foundation.core;
import foundation.settings;

using namespace foundation::core;

export namespace editor
{
    namespace settings = foundation::settings;

    // One scene's saved view state, keyed by its guid.
    struct SceneViewPref
    {
        Guid scene;
        bool showGrid = true;
        void Serialize(ISerializer& ar)
        {
            ar.Key("scene");
            ar.GuidValue(scene);
            foundation::core::Serialize(ar, "showGrid", showGrid);
        }
    };
    inline void Serialize(ISerializer& ar, SceneViewPref& p)
    {
        ar.BeginObject();
        p.Serialize(ar);
        ar.EndObject();
    }

    // The settings section: a flat list of per-scene prefs (linear scan - a project has a handful of
    // scenes, not thousands).
    class SceneViewSettings final : public ISerializable
    {
        RTTI_OBJECT(SceneViewSettings, ISerializable)
    public:
        Array<SceneViewPref> prefs;
        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "prefs", prefs);
        }
    };

    // Register the section type (call before the app loads the per-project store).
    inline void RegisterSceneViewSettingsType()
    {
        GlobalTypeRegistry().Register(SceneViewSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<SceneViewSettings>();
    }

    // Read a scene's saved grid state; returns `fallback` when the store/scene has no saved pref (a
    // nil guid = an unsaved scene = edit-live-only). Never mutates the store.
    [[nodiscard]] inline bool LoadSceneGridPref(settings::Settings* store, const Guid& scene,
                                                bool fallback)
    {
        if (store == nullptr || scene.IsNil())
        {
            return fallback;
        }
        if (const SceneViewSettings* section = store->Find<SceneViewSettings>())
        {
            for (const SceneViewPref& p : section->prefs)
            {
                if (p.scene == scene)
                {
                    return p.showGrid;
                }
            }
        }
        return fallback;
    }

    // Upsert a scene's grid state into the store (marks the section changed). Returns false without
    // touching the store when there is nothing to key by (no store / unsaved scene) - the caller
    // then skips its RequestProjectEditorSettingsSave.
    inline bool SaveSceneGridPref(settings::Settings* store, const Guid& scene, bool showGrid)
    {
        if (store == nullptr || scene.IsNil())
        {
            return false;
        }
        SceneViewSettings& section = store->Section<SceneViewSettings>();
        for (SceneViewPref& p : section.prefs)
        {
            if (p.scene == scene)
            {
                p.showGrid = showGrid;
                store->MarkChanged<SceneViewSettings>();
                return true;
            }
        }
        section.prefs.PushBack(SceneViewPref{scene, showGrid});
        store->MarkChanged<SceneViewSettings>();
        return true;
    }

    RTTI_DEFINE_OBJECT_VERSIONED(SceneViewSettings, "rtti::editor::editor.scene", 1)
}
