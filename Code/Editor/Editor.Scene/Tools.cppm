// Editor::Scene - :tools partition.
//
// SelectTransformTool: the scene viewport's DEFAULT viewport tool (editor.viewporttools) -
// click-pick selection + the transform gizmo, lifted verbatim from the page so the viewport has
// one input-routing path (the tool-mode framework decision, terrain spec 2026-08-10). The page
// keeps camera policy (it masks buttons and nulls the keyboard while the camera owns the mouse);
// this tool owns everything after that: gizmo session, pick, overlay, status text.
//
// Behavior contract (identical to the pre-framework page):
//   - gizmo consumes the pointer while a handle is hot or a drag runs; picking is suppressed;
//   - editingLocked (Simulate) = the gizmo goes read-only (pose sync only, no drags - mapped to
//     the controller's pointer-less update) but PICKING still works (selection is not an edit);
//   - pick: closest entity by screen-constant-ish radius; Ctrl toggles, click-nothing clears
//     (unless Ctrl); requires the pointer OVER the viewport.

module;
#include "Core/Prelude.h"

export module editor.scene:tools;

import foundation.core;
import foundation.render;
import editor.core;
import editor.viewporttools;
import :edit;
import :gizmo;

using namespace foundation::core;
namespace core = foundation::core;

export namespace editor
{
    class SelectTransformTool final : public IViewportTool
    {
    public:
        explicit SelectTransformTool(SceneEditContext& edit) : m_edit(&edit), m_gizmos(edit) {}

        [[nodiscard]] StringView Id() const override { return u8"select"; }
        [[nodiscard]] StringView DisplayName() const override { return u8"Select"; }

        [[nodiscard]] bool Update(const ViewportToolInput& input) override;

        void Draw(render::debug::DebugDraw& drawList) override { m_gizmos.Draw(drawList); }

        [[nodiscard]] StringView StatusText() const override
        {
            return m_gizmos.IsActive() ? m_gizmos.StatusText() : StringView{};
        }

        void OnDeactivate() override;

        /// The toolbar drives mode/space through the controller directly (W/E/R/X parity).
        [[nodiscard]] GizmoController& Gizmos() noexcept { return m_gizmos; }

    private:
        void PickOnClick(const ViewportToolInput& input);

        SceneEditContext* m_edit; // borrowed (the page owns it; tool dies with the page)
        GizmoController m_gizmos;
    };
}
