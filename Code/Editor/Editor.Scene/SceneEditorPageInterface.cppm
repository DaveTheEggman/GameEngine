// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :scene_page_interface partition.
//
// ISceneEditorPage: what a scene (or prefab) page lets the rest of the editor act through,
// page by page - the multi-scene rule makes everything here THIS page's. Published on the page
// (EditorPage::Provide) and found from any EditorPage* through EditorPage::Service, so a
// module that holds a page needs no cast and no knowledge of the page class.
//
// Two surfaces exist for acting on a scene page, and this is the page-level one: the same the
// page gives its OWN tools (its SceneEditContext, with the typed command vocabulary every edit
// goes through). The framework-level one, ViewportToolHostContext, is what a domain's provider
// tools receive at creation, limited to framework types; it is not reachable from here on
// purpose. Simulation, the gizmo, the markers overlay and the prefab flows are page concerns
// (they lock the command stack, drive the toolbar, open the page's dialogs), so their control
// is part of this surface, not of the edit context; the scene editor's actions bind through it.
module;
#include "Core/Prelude.h"

export module editor.scene:scene_page_interface;

import foundation.core;
import editor.core;
import :edit;
import :gizmo;

using namespace foundation::core;

export namespace editor
{
    class ISceneEditorPage : public IPageService
    {
    public:
        /// This page's scene mutation mediator: its live scene, its command stack, its entity
        /// selection. Every edit goes through it as an undoable command.
        [[nodiscard]] virtual SceneEditContext& EditContext() noexcept = 0;

        /// Edit-mode Simulate: snapshot, run the live scene, restore on stop. Start and stop are
        /// no-ops when already in that state; pause holds a running simulation.
        virtual void StartSimulation() = 0;
        virtual void StopSimulation() = 0;
        virtual void PauseSimulation(bool paused) = 0;
        [[nodiscard]] virtual bool IsSimulating() const noexcept = 0;
        [[nodiscard]] virtual bool IsPaused() const noexcept = 0;

        /// The select tool's gizmo (mode, space); null on a page without a viewport.
        [[nodiscard]] virtual GizmoController* Gizmos() noexcept = 0;

        /// The entity markers overlay (every entity's marker in the viewport, not only the
        /// selected ones).
        [[nodiscard]] virtual bool MarkersShown() const noexcept = 0;
        virtual void SetMarkersShown(bool shown) = 0;

        /// The prefab flows the page owns (they open the page's dialogs and write assets):
        /// a prefab asset from an entity subtree; an instance spawned under `parent` (nil =
        /// the scene root) via the asset picker; an instance's deltas written back to its
        /// prefab; an instance's deltas discarded.
        virtual void CreatePrefabFromEntity(const Guid& entity) = 0;
        virtual void PickAndSpawnPrefab(const Guid& parent) = 0;
        virtual void ApplyInstanceToPrefab(const Guid& root) = 0;
        virtual void RevertInstance(const Guid& root) = 0;
    };
}
