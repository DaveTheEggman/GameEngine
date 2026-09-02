// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - implementation unit: ScreenStack push/pop/transition drive, UIScreen markup-attribute
// parsers, `<screen>` markup registration, and the UIScreen RTTI definition. Kept out of the interface
// units (GCC gcm-cluster hygiene + the heavier logic is not header material).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.ui.gamekit;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

namespace foundation::ui::gamekit
{
    // ===================================================================================== UIScreen ===

    RTTI_DEFINE_OBJECT(UIScreen, "rtti::ui.gamekit")

    namespace
    {
        [[nodiscard]] TransitionKind ParseTransitionKind(StringView value)
        {
            // Accept "kind" (duration stays at the descriptor default). Whitespace-trimmed compare.
            const auto eq = [value](const char8_t* s) { return value == StringView(s); };
            if (eq(u8"fade"))
            {
                return TransitionKind::Fade;
            }
            if (eq(u8"slide-left"))
            {
                return TransitionKind::SlideLeft;
            }
            if (eq(u8"slide-right"))
            {
                return TransitionKind::SlideRight;
            }
            if (eq(u8"slide-up"))
            {
                return TransitionKind::SlideUp;
            }
            if (eq(u8"slide-down"))
            {
                return TransitionKind::SlideDown;
            }
            if (eq(u8"scale"))
            {
                return TransitionKind::Scale;
            }
            return TransitionKind::None; // "none" or unrecognized
        }
    }

    void UIScreen::SetModeFromString(StringView value)
    {
        if (value == StringView(u8"overlay"))
        {
            m_mode = ScreenMode::Overlay;
        }
        else if (value == StringView(u8"opaque"))
        {
            m_mode = ScreenMode::Opaque;
        }
        else
        {
            m_mode = ScreenMode::Modal; // default
        }
    }

    void UIScreen::SetInTransitionFromString(StringView value)
    {
        m_in.kind = ParseTransitionKind(value);
    }
    void UIScreen::SetOutTransitionFromString(StringView value)
    {
        m_out.kind = ParseTransitionKind(value);
    }
    void UIScreen::SetTransitionFromString(StringView value)
    {
        const TransitionKind kind = ParseTransitionKind(value);
        m_in.kind = kind;
        m_out.kind = kind;
    }

    void RegisterGamekitMarkup()
    {
        static const bool once = []()
        {
            MarkupRegistry::RegisterView(
                u8"screen", [](IAllocator& allocator) -> RefPtr<View> { return MakeRef<UIScreen>(allocator); });
            MarkupRegistry::RegisterProperty(u8"screen", u8"mode", [](View* v, StringView val)
                                             { if (auto* s = Cast<UIScreen>(v)) s->SetModeFromString(val); });
            MarkupRegistry::RegisterProperty(u8"screen", u8"transition", [](View* v, StringView val)
                                             { if (auto* s = Cast<UIScreen>(v)) s->SetTransitionFromString(val); });
            MarkupRegistry::RegisterProperty(u8"screen", u8"in-transition", [](View* v, StringView val)
                                             { if (auto* s = Cast<UIScreen>(v)) s->SetInTransitionFromString(val); });
            MarkupRegistry::RegisterProperty(u8"screen", u8"out-transition", [](View* v, StringView val)
                                             { if (auto* s = Cast<UIScreen>(v)) s->SetOutTransitionFromString(val); });
            MarkupRegistry::RegisterProperty(u8"screen", u8"default-focus", [](View* v, StringView val)
                                             { if (auto* s = Cast<UIScreen>(v)) s->SetDefaultFocus(val); });
            return true;
        }();
        (void)once;
    }

    // ================================================================================== ScreenStack ===

    UIContext* ScreenStack::Ctx() const noexcept
    {
        return (m_root != nullptr) ? m_root->Context : nullptr;
    }

    FocusManager* ScreenStack::Focus() const noexcept
    {
        UIContext* c = Ctx();
        return (c != nullptr) ? c->GetFocusManager() : nullptr;
    }

    bool ScreenStack::SafeToMutateNow() const
    {
        UIContext* c = Ctx();
        // No context (headless / a root not yet added to a context) -> nothing is dispatching, run now.
        return (c == nullptr) || (c->CurrentPhase() == UIContext::Phase::Idle);
    }

    void ScreenStack::RunStructural(Function<void()> action)
    {
        if (SafeToMutateNow())
        {
            action();
            return;
        }
        Ctx()->MutationQueueRef().QueueAction(Move(action));
    }

    UIScreen* ScreenStack::Top() const noexcept
    {
        return m_entries.IsEmpty() ? nullptr : m_entries[m_entries.Size() - 1].screen.Get();
    }

    void ScreenStack::RecomputeVisibility()
    {
        bool covered = false;
        for (usize count = m_entries.Size(), k = 0; k < count; ++k)
        {
            const usize i = count - 1 - k; // top-down
            UIScreen* s = m_entries[i].screen.Get();
            s->Visibility = covered ? Visibility::Hidden : Visibility::Visible;
            if (s->HidesBelow())
            {
                covered = true; // everything below a fully-covering screen is hidden
            }
        }
    }

    void ScreenStack::PlayTransition(UIScreen* screen, const TransitionDesc& desc, bool isEnter,
                                     Function<void()> onDone)
    {
        UIContext* c = Ctx();
        AnimationManager* am = (c != nullptr) ? c->Animations() : nullptr;
        if (am == nullptr || screen == nullptr || desc.kind == TransitionKind::None)
        {
            if (onDone)
            {
                onDone();
            }
            return;
        }

        const EasingFunction ease =
            desc.easing != nullptr ? desc.easing : (isEnter ? Easing::EaseOutCubic : Easing::EaseInCubic);
        const f32 dur = desc.duration > 0.0f ? desc.duration : 0.2f;
        const Float2 size = (m_root != nullptr) ? m_root->LogicalSize() : Float2{0.0f, 0.0f};

        UniquePtr<Animation> anim;
        switch (desc.kind)
        {
        case TransitionKind::Fade:
            anim = isEnter ? ViewAnimator::FadeIn(screen, dur, ease)
                           : ViewAnimator::FadeOut(screen, dur, ease);
            break;
        case TransitionKind::SlideLeft:
            anim = isEnter ? ViewAnimator::TranslateX(screen, size.x, 0.0f, dur, ease)
                           : ViewAnimator::TranslateX(screen, 0.0f, size.x, dur, ease);
            break;
        case TransitionKind::SlideRight:
            anim = isEnter ? ViewAnimator::TranslateX(screen, -size.x, 0.0f, dur, ease)
                           : ViewAnimator::TranslateX(screen, 0.0f, -size.x, dur, ease);
            break;
        case TransitionKind::SlideUp:
            anim = isEnter ? ViewAnimator::TranslateY(screen, size.y, 0.0f, dur, ease)
                           : ViewAnimator::TranslateY(screen, 0.0f, size.y, dur, ease);
            break;
        case TransitionKind::SlideDown:
            anim = isEnter ? ViewAnimator::TranslateY(screen, -size.y, 0.0f, dur, ease)
                           : ViewAnimator::TranslateY(screen, 0.0f, -size.y, dur, ease);
            break;
        case TransitionKind::Scale:
            anim = isEnter ? ViewAnimator::ScaleTo(screen, 0.9f, 1.0f, dur, ease)
                           : ViewAnimator::ScaleTo(screen, 1.0f, 0.9f, dur, ease);
            am->Add(isEnter ? ViewAnimator::FadeIn(screen, dur, ease)
                            : ViewAnimator::FadeOut(screen, dur, ease));
            break;
        default:
            break;
        }

        if (!anim)
        {
            if (onDone)
            {
                onDone();
            }
            return;
        }
        if (onDone)
        {
            anim->OnComplete.Add([done = Move(onDone)](Animation*) { done(); });
        }
        am->Add(Move(anim));
    }

    UIScreen* ScreenStack::Push(RefPtr<UIScreen> screen)
    {
        if (m_root == nullptr || !screen)
        {
            return screen.Get();
        }
        UIScreen* raw = screen.Get();

        // Synchronous bookkeeping so Top()/Count() are correct immediately.
        Entry entry;
        entry.screen = screen; // owning ref
        if (FocusManager* fm = Focus())
        {
            entry.savedFocus = fm->SaveAndClearFocus(); // capture focus to restore when THIS screen pops
        }
        UIScreen* prevTop = Top();
        m_entries.PushBack(Move(entry));

        // Deferred (or inline-when-safe) structural work: attach + shield + visibility + lifecycle +
        // default focus + in-transition.
        RunStructural(
            [this, raw, prevTop]()
            {
                if (m_root == nullptr)
                {
                    return;
                }
                if (prevTop != nullptr)
                {
                    prevTop->OnHidden();
                }
                raw->IsHitTestVisible = raw->ShieldsInput(); // Modal/Opaque eat input below; Overlay passes
                m_root->AddView(raw);
                RecomputeVisibility();
                raw->OnEnter();
                raw->OnShown();
                if (FocusManager* fm = Focus())
                {
                    View* target = nullptr;
                    if (!raw->DefaultFocus().IsEmpty())
                    {
                        target = raw->FindByName(raw->DefaultFocus());
                    }
                    if (target != nullptr)
                    {
                        fm->SetFocus(target, FocusSource::Programmatic);
                    }
                    else
                    {
                        fm->FocusFirstIn(raw);
                    }
                }
                PlayTransition(raw, raw->InTransition(), /*isEnter*/ true, {});
            });
        return raw;
    }

    void ScreenStack::Pop()
    {
        if (m_entries.IsEmpty())
        {
            return;
        }
        Entry popped = Move(m_entries[m_entries.Size() - 1]);
        m_entries.PopBack(); // Top()/Count() reflect the new top synchronously
        RefPtr<UIScreen> keep = popped.screen;      // hold alive through the transition
        UIScreen* screen = keep.Get();
        const FocusManager::SavedFocus saved = popped.savedFocus;

        // Play the out-transition; on completion detach + lifecycle + restore visibility/focus.
        PlayTransition(
            screen, screen->OutTransition(), /*isEnter*/ false,
            [this, keep, saved]()
            {
                RunStructural(
                    [this, keep, saved]()
                    {
                        if (m_root != nullptr)
                        {
                            m_root->RemoveView(keep.Get());
                        }
                        keep->OnExit();
                        RecomputeVisibility();
                        if (UIScreen* newTop = Top())
                        {
                            newTop->OnShown();
                        }
                        if (FocusManager* fm = Focus())
                        {
                            fm->RestoreFocus(saved);
                        }
                    });
            });
    }

    UIScreen* ScreenStack::Replace(RefPtr<UIScreen> screen)
    {
        if (!m_entries.IsEmpty())
        {
            // Remove the current top with no out-transition (the new screen takes over immediately).
            Entry top = Move(m_entries[m_entries.Size() - 1]);
            m_entries.PopBack();
            RefPtr<UIScreen> keep = top.screen;
            RunStructural(
                [this, keep]()
                {
                    if (m_root != nullptr)
                    {
                        m_root->RemoveView(keep.Get());
                    }
                    keep->OnExit();
                });
        }
        return Push(Move(screen));
    }

    void ScreenStack::Clear()
    {
        while (!m_entries.IsEmpty())
        {
            Entry e = Move(m_entries[m_entries.Size() - 1]);
            m_entries.PopBack();
            RefPtr<UIScreen> keep = e.screen;
            RunStructural(
                [this, keep]()
                {
                    if (m_root != nullptr)
                    {
                        m_root->RemoveView(keep.Get());
                    }
                    keep->OnExit();
                });
        }
    }

    bool ScreenStack::HandleBack()
    {
        if (m_entries.Size() <= 1)
        {
            return false; // never pop the last screen out from under the player
        }
        Pop();
        return true;
    }
}
