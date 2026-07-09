// Draconic GUI - :actions partition
//
// Concrete actions that animate a Node: Move / Fade / Scale (interpolated), Delay,
// Runnable (a deferred callback), and Sequence (run children in order). Derived from
// eepp's scene/actions/*. These live here (not in :action) because they dereference the
// target Node, so this partition imports the full :node.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:actions;

import draconic.core;   // Float2, Duration, Lerp, Function, RefPtr, Array, Move
import :action;
import :node;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    // Interpolates the target's position from `from` to `to`.
    class MoveAction : public ActionInterpolation
    {
        DRACONIC_OBJECT(MoveAction, ActionInterpolation)
    public:
        MoveAction(core::Float2 from, core::Float2 to, core::Duration duration) noexcept
            : ActionInterpolation(duration), m_from(from), m_to(to) {}
    protected:
        void OnStep(f32 t) override { if (m_target) m_target->SetPosition(core::Lerp(m_from, m_to, t)); }
    private:
        core::Float2 m_from, m_to;
    };
    DRACONIC_DEFINE_OBJECT(MoveAction, "draconic::gui")

    // Interpolates the target's alpha from `from` to `to`.
    class FadeAction : public ActionInterpolation
    {
        DRACONIC_OBJECT(FadeAction, ActionInterpolation)
    public:
        FadeAction(f32 from, f32 to, core::Duration duration) noexcept
            : ActionInterpolation(duration), m_from(from), m_to(to) {}
    protected:
        void OnStep(f32 t) override { if (m_target) m_target->SetAlpha(m_from + (m_to - m_from) * t); }
    private:
        f32 m_from, m_to;
    };
    DRACONIC_DEFINE_OBJECT(FadeAction, "draconic::gui")

    // Interpolates the target's scale from `from` to `to`.
    class ScaleAction : public ActionInterpolation
    {
        DRACONIC_OBJECT(ScaleAction, ActionInterpolation)
    public:
        ScaleAction(core::Float2 from, core::Float2 to, core::Duration duration) noexcept
            : ActionInterpolation(duration), m_from(from), m_to(to) {}
    protected:
        void OnStep(f32 t) override { if (m_target) m_target->SetScale(core::Lerp(m_from, m_to, t)); }
    private:
        core::Float2 m_from, m_to;
    };
    DRACONIC_DEFINE_OBJECT(ScaleAction, "draconic::gui")

    // Waits a duration, then completes (no visible effect).
    class DelayAction : public ActionInterpolation
    {
        DRACONIC_OBJECT(DelayAction, ActionInterpolation)
    public:
        explicit DelayAction(core::Duration duration) noexcept : ActionInterpolation(duration) {}
    protected:
        void OnStep(f32) override {}
    };
    DRACONIC_DEFINE_OBJECT(DelayAction, "draconic::gui")

    // Runs a callback once, after an optional delay.
    class RunnableAction : public ActionInterpolation
    {
        DRACONIC_OBJECT(RunnableAction, ActionInterpolation)
    public:
        explicit RunnableAction(core::Function<void()> fn, core::Duration delay = core::Duration{}) noexcept
            : ActionInterpolation(delay), m_fn(core::Move(fn)) {}
    protected:
        void OnStep(f32 t) override { if (t >= 1.0f && !m_ran && m_fn) { m_ran = true; m_fn(); } }
    private:
        core::Function<void()> m_fn;
        bool m_ran = false;
    };
    DRACONIC_DEFINE_OBJECT(RunnableAction, "draconic::gui")

    // Runs child actions one after another; done when the last finishes.
    class SequenceAction : public Action
    {
        DRACONIC_OBJECT(SequenceAction, Action)
    public:
        void Add(RefPtr<Action> action) { m_children.PushBack(core::Move(action)); }

        void Start() override
        {
            m_index = 0;
            m_done = m_children.Size() == 0;
            if (!m_done) StartCurrent();
        }
        void Stop() override { m_done = true; }

        void Update(core::Duration elapsed) override
        {
            if (m_done || m_index >= m_children.Size()) return;
            Action* current = m_children[m_index].Get();
            current->Update(elapsed);
            if (current->IsDone())
            {
                ++m_index;
                if (m_index >= m_children.Size()) { m_done = true; FireDone(); }
                else StartCurrent();
            }
        }

        [[nodiscard]] bool IsDone() override { return m_done; }
        [[nodiscard]] f32 GetCurrentProgress() override
        {
            return m_children.Size() ? static_cast<f32>(m_index) / static_cast<f32>(m_children.Size()) : 1.0f;
        }
        [[nodiscard]] core::Duration GetTotalTime() override
        {
            core::Duration total{};
            for (const RefPtr<Action>& c : m_children) total += c->GetTotalTime();
            return total;
        }

    protected:
        void OnTargetChange() override { for (const RefPtr<Action>& c : m_children) c->SetTarget(m_target); }

    private:
        void StartCurrent()
        {
            if (m_index < m_children.Size())
            {
                m_children[m_index]->SetTarget(m_target);
                m_children[m_index]->Start();
            }
        }

        Array<RefPtr<Action>> m_children;
        usize m_index = 0;
        bool m_done = false;
    };
    DRACONIC_DEFINE_OBJECT(SequenceAction, "draconic::gui")
}
