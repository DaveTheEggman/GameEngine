// Editor::App - the `editor.app:floating_panel` partition.
//
// A draggable / resizable / collapsible / closable panel that floats OVER another view (not an OS
// window, not docking-managed - that is the separate DockableWindow/IDockableWindowHost world). Built
// for the viewport-tool-panel Float placement (a brush palette floating over the 3D viewport) but
// generic: it holds arbitrary content and emits OnClose. Behaviours:
//   - DRAG: grab the header title strip; the panel moves via Transform.Translation, CLAMPED to its
//     parent so it can't leave the pane (a sibling SplitView pane would draw over it + steal input).
//   - RESIZE: a bottom-right grip; sets an explicit size. Until then the panel SIZES TO CONTENT.
//   - COLLAPSE: a header toggle hides the body, leaving just the header strip.
//   - CLOSE: a header button fires OnClose (the tool-panel host deactivates the tool).
// Pure UI (foundation.ui) - the drag/resize/collapse live here; the CONSUMER decides what close means.

module;
#include "Core/Prelude.h"

export module editor.app:floating_panel;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace editor
{
    class FloatingPanel final : public foundation::ui::Panel
    {
    public:
        explicit FloatingPanel(StringView title);

        /// Replace the body content (below the header). Null clears it.
        void SetContent(RefPtr<foundation::ui::View> content);

        /// Fired by the header's close button. The consumer decides the effect (the tool-panel host
        /// deactivates the active tool, which unmounts this panel).
        foundation::ui::Event<void()> OnClose;

        void SetCollapsed(bool collapsed);
        [[nodiscard]] bool IsCollapsed() const noexcept { return m_collapsed; }

        // --- called by the internal drag/resize handles (public so the module-internal strips reach
        //     them; not part of the consumer-facing surface) ---
        void DragBy(f32 dx, f32 dy);
        void ResizeBy(f32 dx, f32 dy);

    private:
        void ClampToParent();
        void ApplySize();

        RefPtr<foundation::ui::FlexLayout> m_body;    // content host (Gone when collapsed)
        RefPtr<foundation::ui::Button> m_collapseBtn; // toggles collapse; label - / +
        bool m_collapsed = false;
        f32 m_width = 0.0f;  // explicit size after a resize (0 = wrap to content)
        f32 m_height = 0.0f;
    };
}
