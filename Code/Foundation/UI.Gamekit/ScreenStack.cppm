// UI.Gamekit - :stack partition
//
// ScreenStack: manages a stack of UIScreens over a RootView (the screen tier's, wired by the host).
// push / pop / replace / clear / top. The top screen owns focus + input per its mode; screens below are
// shielded (Modal/Opaque) and/or hidden (Opaque). Focus is saved on push and restored on pop (CORE
// FocusManager's caller-held-token model). Transitions play on the CORE Animation layer.
//
// STRUCTURAL SAFETY: every view-tree mutation (AddView/RemoveView) runs inline only when the context is
// Idle; when called mid-input-dispatch (e.g. from a button's OnClick) it is deferred through the
// UIContext mutation queue - the view-destroying-action rule. The stack's own bookkeeping (m_entries)
// updates synchronously so Top()/Count() are correct immediately.

module;
#include "Core/Prelude.h"

export module foundation.ui.gamekit:stack;

import foundation.core;
import foundation.ui;
import :screen;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    class ScreenStack
    {
    public:
        ScreenStack() = default;

        // The screen-tier root this stack manages (borrowed; the host owns it). Screens are added as
        // its children. Null until attached -> all ops are safe no-ops.
        void Attach(foundation::ui::RootView* root) noexcept { m_root = root; }
        [[nodiscard]] foundation::ui::RootView* Root() const noexcept { return m_root; }

        // Push a screen on top of the stack. The stack takes an owning ref; the raw pointer is returned
        // for immediate addressing (its subtree is searchable even before the deferred attach lands).
        UIScreen* Push(RefPtr<UIScreen> screen);
        // Pop the top screen: plays its out-transition, then detaches it and restores focus. No-op when
        // empty.
        void Pop();
        // Replace the top screen: removes the current top (no out-transition) then pushes the new one.
        UIScreen* Replace(RefPtr<UIScreen> screen);
        // Pop every screen (no transitions).
        void Clear();

        [[nodiscard]] UIScreen* Top() const noexcept;
        [[nodiscard]] usize Count() const noexcept { return m_entries.Size(); }

        // Back/Cancel handling: pop the top screen unless it is the last one (a menu Back that would
        // empty the stack is ignored - the host decides what "exit the last screen" means). Returns
        // true if a screen was popped.
        bool HandleBack();

    private:
        struct Entry
        {
            RefPtr<UIScreen> screen;
            foundation::ui::FocusManager::SavedFocus savedFocus{};
        };

        [[nodiscard]] foundation::ui::UIContext* Ctx() const noexcept;
        [[nodiscard]] foundation::ui::FocusManager* Focus() const noexcept;
        // True when a structural mutation can run inline (no context, or the context is Idle). When
        // false (mid Layout/Drawing/input dispatch) the caller defers through the mutation queue.
        [[nodiscard]] bool SafeToMutateNow() const;
        // Run a structural action now if safe, else queue it on the context mutation queue.
        void RunStructural(Function<void()> action);
        // Start a transition on `screen`; call `onDone` when it finishes (immediately if there is no
        // context/AnimationManager or the kind is None).
        void PlayTransition(UIScreen* screen, const TransitionDesc& desc, bool isEnter,
                            Function<void()> onDone);
        // Recompute per-screen visibility from the top down: screens show until (and including) the
        // first Opaque, everything below it is Hidden. Deterministic - called after push + pop.
        void RecomputeVisibility();

        foundation::ui::RootView* m_root = nullptr;
        Array<Entry> m_entries;
    };
}
