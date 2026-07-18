// Draconic::Input - :runtime partition.
//
// ActionRuntime (docs/design/input.md §3.2): per-frame evaluation of an InputMap against
// polled shell device facades. Godot's value model (per-device OR/MAX folding, [0,1]
// strength, frame-counter-exact edges, circular dead zones) on ez's set model (all enabled
// sets evaluate; queries resolve flat by priority; exclusive-set push with HELD-SUPPRESSION
// latching - an action suppressed while physically held stays released until the physical
// release, so closing a menu never re-fires a held "Fire").
//
// Devices arrive through IInputSourceProvider: the game host passes the raw shell, play-in-
// editor passes the Game viewport's gated InputSurface facades - fixing, by construction,
// Sedulous's "editor viewport forwards nothing".

module;
#include "Core/Prelude.h"
#include <cmath>

export module draconic.input:runtime;

import draconic.core;
import draconic.shell;
import :model;

using namespace draconic::core;

export namespace draconic::input
{
    namespace dshell = draconic::shell;

    // The device seam. All accessors may return null / zero - devices come and go (hotplug,
    // unfocused editor viewport) and evaluation treats absence as "released".
    class IInputSourceProvider
    {
    public:
        virtual ~IInputSourceProvider() = default;
        [[nodiscard]] virtual dshell::IKeyboard* Keyboard() = 0;
        [[nodiscard]] virtual dshell::IMouse* Mouse() = 0;
        [[nodiscard]] virtual i32 GamepadCount() const = 0;
        [[nodiscard]] virtual dshell::IGamepad* Gamepad(i32 index) = 0;
    };

    // The common case: the whole app's devices, straight off the shell.
    class ShellInputSource final : public IInputSourceProvider
    {
    public:
        explicit ShellInputSource(dshell::IInputManager* input) : m_input(input) {}
        void SetInput(dshell::IInputManager* input) noexcept { m_input = input; }
        [[nodiscard]] dshell::IKeyboard* Keyboard() override { return m_input != nullptr ? m_input->Keyboard() : nullptr; }
        [[nodiscard]] dshell::IMouse* Mouse() override { return m_input != nullptr ? m_input->Mouse() : nullptr; }
        [[nodiscard]] i32 GamepadCount() const override { return m_input != nullptr ? m_input->GamepadCount() : 0; }
        [[nodiscard]] dshell::IGamepad* Gamepad(i32 index) override { return m_input != nullptr ? m_input->GetGamepad(index) : nullptr; }

    private:
        dshell::IInputManager* m_input = nullptr;   // borrowed
    };

    // A resolved action name: hash computed once, candidates (same name across sets) cached
    // sorted by set priority. Queries are array lookups, never per-call string compares.
    struct ActionRef
    {
        u32 index = kInvalid;   // into the runtime's candidate table
        static constexpr u32 kInvalid = 0xFFFFFFFFu;
        [[nodiscard]] bool IsValid() const noexcept { return index != kInvalid; }
    };

    class ActionRuntime
    {
    public:
        static constexpr f32 kPressPoint = 0.5f;

        /// Installs (copies) the map and rebuilds all state. Every set starts ENABLED.
        void SetMap(const InputMap& map)
        {
            m_map = map;
            m_states.Clear();
            m_setEnabled.Clear();
            m_exclusiveStack.Clear();
            m_refs.Clear();
            usize actionTotal = 0;
            for (const ActionSet& set : m_map.sets) { actionTotal += set.actions.Size(); }
            m_states.Resize(actionTotal);
            m_setEnabled.Resize(m_map.sets.Size());
            for (usize i = 0; i < m_setEnabled.Size(); ++i) { m_setEnabled[i] = 1u; }
        }
        [[nodiscard]] const InputMap& Map() const noexcept { return m_map; }

        // ---- sets ----
        void EnableSet(StringView name, bool enabled = true)
        {
            for (usize i = 0; i < m_map.sets.Size(); ++i)
            {
                if (m_map.sets[i].name.AsView() == name) { m_setEnabled[i] = enabled ? 1u : 0u; }
            }
        }
        void DisableSet(StringView name) { EnableSet(name, false); }
        [[nodiscard]] bool IsSetEnabled(StringView name) const
        {
            for (usize i = 0; i < m_map.sets.Size(); ++i)
            {
                if (m_map.sets[i].name.AsView() == name) { return m_setEnabled[i] != 0u; }
            }
            return false;
        }

        /// Modal contexts: while the stack is non-empty, only the TOP set's actions read
        /// active; everything else reads released (with proper release edges + latching).
        /// Every exclusive TRANSITION additionally latches all physically-held actions -
        /// ez's require-key-up-on-activation: a held Fire neither Confirms the menu that
        /// just opened nor re-fires when it closes; modal boundaries demand a fresh press.
        void PushExclusiveSet(StringView name)
        {
            m_exclusiveStack.PushBack(String(name));
            m_latchHeldOnce = true;
        }
        void PopExclusiveSet()
        {
            if (!m_exclusiveStack.IsEmpty()) { m_exclusiveStack.PopBack(); m_latchHeldOnce = true; }
        }
        [[nodiscard]] usize ExclusiveDepth() const noexcept { return m_exclusiveStack.Size(); }

        // ---- resolution ----
        /// Flat resolution (the approved model): the name is looked up across ALL sets;
        /// at query time the highest-priority candidate in an ENABLED set answers.
        [[nodiscard]] ActionRef Resolve(StringView name)
        {
            for (u32 i = 0; i < static_cast<u32>(m_refs.Size()); ++i)
            {
                if (m_refs[i].name.AsView() == name) { return ActionRef{ i }; }
            }
            RefEntry entry;
            entry.name = String(name);
            usize flat = 0;
            for (u32 s = 0; s < static_cast<u32>(m_map.sets.Size()); ++s)
            {
                for (u32 a = 0; a < static_cast<u32>(m_map.sets[s].actions.Size()); ++a, ++flat)
                {
                    if (m_map.sets[s].actions[a].name.AsView() == name)
                    {
                        entry.candidates.PushBack(Candidate{ s, static_cast<u32>(flat) });
                    }
                }
            }
            // Highest set priority first (stable for ties: map order).
            for (usize i = 1; i < entry.candidates.Size(); ++i)
            {
                for (usize j = i; j > 0; --j)
                {
                    const i32 pa = m_map.sets[entry.candidates[j - 1].set].priority;
                    const i32 pb = m_map.sets[entry.candidates[j].set].priority;
                    if (pb > pa) { Swap(entry.candidates[j - 1], entry.candidates[j]); }
                    else { break; }
                }
            }
            m_refs.PushBack(static_cast<RefEntry&&>(entry));
            return ActionRef{ static_cast<u32>(m_refs.Size() - 1) };
        }

        // ---- per-frame evaluation ----
        void Update(IInputSourceProvider& devices, f32 deltaTime)
        {
            ++m_frame;
            const i32 exclusiveTop = ExclusiveTopSet();
            usize flat = 0;
            for (usize s = 0; s < m_map.sets.Size(); ++s)
            {
                // Disabled behaves exactly like exclusive-suppressed: released + latching.
                const bool suppressed = (m_setEnabled[s] == 0u)
                    || (exclusiveTop >= 0 && static_cast<i32>(s) != exclusiveTop);
                for (usize a = 0; a < m_map.sets[s].actions.Size(); ++a, ++flat)
                {
                    EvaluateAction(m_map.sets[s].actions[a], m_states[flat], devices,
                                   deltaTime, suppressed, m_latchHeldOnce);
                }
            }
            m_latchHeldOnce = false;
        }
        [[nodiscard]] u64 Frame() const noexcept { return m_frame; }

        // ---- queries ----
        [[nodiscard]] bool IsDown(ActionRef ref) const
        {
            const ActionState* state = StateFor(ref);
            return state != nullptr && state->pressed;
        }
        [[nodiscard]] bool WasPressed(ActionRef ref) const
        {
            const ActionState* state = StateFor(ref);
            return state != nullptr && state->pressedFrame == m_frame;
        }
        [[nodiscard]] bool WasReleased(ActionRef ref) const
        {
            const ActionState* state = StateFor(ref);
            return state != nullptr && state->releasedFrame == m_frame;
        }
        [[nodiscard]] f32 Value(ActionRef ref) const
        {
            const ActionState* state = StateFor(ref);
            return state != nullptr ? state->value.x : 0.0f;
        }
        [[nodiscard]] Float2 Value2D(ActionRef ref) const
        {
            const ActionState* state = StateFor(ref);
            return state != nullptr ? state->value : Float2{ 0.0f, 0.0f };
        }

        /// Ad-hoc composition helpers (Godot): a signed axis from two actions, a vector
        /// from four, with circular length clamping.
        [[nodiscard]] f32 Axis(ActionRef negative, ActionRef positive) const
        {
            return Value(positive) - Value(negative);
        }
        [[nodiscard]] Float2 Vector2(ActionRef negX, ActionRef posX, ActionRef negY, ActionRef posY) const
        {
            Float2 v{ Value(posX) - Value(negX), Value(posY) - Value(negY) };
            const f32 length = std::sqrt(v.x * v.x + v.y * v.y);
            if (length > 1.0f) { v.x /= length; v.y /= length; }
            return v;
        }

    private:
        struct ActionState
        {
            Float2 value{ 0.0f, 0.0f };      // post-processor, post-suppression
            Float2 smoothed{ 0.0f, 0.0f };   // smoothing integrator (pre-suppression)
            bool pressed = false;
            bool latched = false;            // held through a suppression: stays released
            u64 pressedFrame = 0;
            u64 releasedFrame = 0;
        };
        struct Candidate { u32 set = 0; u32 flatIndex = 0; };
        struct RefEntry { String name; Array<Candidate> candidates; };

        [[nodiscard]] i32 ExclusiveTopSet() const
        {
            if (m_exclusiveStack.IsEmpty()) { return -1; }
            const StringView top = m_exclusiveStack[m_exclusiveStack.Size() - 1].AsView();
            for (usize i = 0; i < m_map.sets.Size(); ++i)
            {
                if (m_map.sets[i].name.AsView() == top) { return static_cast<i32>(i); }
            }
            return -1;
        }

        [[nodiscard]] const ActionState* StateFor(ActionRef ref) const
        {
            if (!ref.IsValid() || ref.index >= m_refs.Size()) { return nullptr; }
            const RefEntry& entry = m_refs[ref.index];
            for (const Candidate& candidate : entry.candidates)
            {
                if (m_setEnabled[candidate.set] != 0u) { return &m_states[candidate.flatIndex]; }
            }
            return nullptr;
        }

        [[nodiscard]] static f32 ApplyDeadZone(f32 v, f32 deadZone)
        {
            const f32 magnitude = std::fabs(v);
            if (magnitude <= deadZone) { return 0.0f; }
            const f32 rescaled = (magnitude - deadZone) / (1.0f - deadZone);
            return v < 0.0f ? -rescaled : rescaled;
        }

        [[nodiscard]] static Float2 ApplyCircularDeadZone(Float2 v, f32 deadZone)
        {
            const f32 length = std::sqrt(v.x * v.x + v.y * v.y);
            if (length <= deadZone) { return Float2{ 0.0f, 0.0f }; }
            const f32 rescaled = Min((length - deadZone) / (1.0f - deadZone), 1.0f);
            const f32 factor = rescaled / length;
            return Float2{ v.x * factor, v.y * factor };
        }

        struct Contribution { Float2 value{ 0.0f, 0.0f }; bool digitalDown = false; };

        [[nodiscard]] static bool KeyDown(dshell::IKeyboard* keyboard, u32 code, u32 modifiers)
        {
            if (keyboard == nullptr) { return false; }
            if (!keyboard->IsKeyDown(static_cast<dshell::KeyCode>(code))) { return false; }
            if (modifiers != 0u
                && (static_cast<u32>(keyboard->Modifiers()) & modifiers) != modifiers) { return false; }
            return true;
        }

        [[nodiscard]] static Contribution EvaluateBinding(const Binding& b, IInputSourceProvider& devices)
        {
            Contribution out;
            const f32 sign = b.invert ? -1.0f : 1.0f;
            switch (b.source)
            {
                case BindingSource::Key:
                {
                    if (KeyDown(devices.Keyboard(), b.code, b.modifiers))
                    {
                        out.value.x = b.scale * sign;
                        out.digitalDown = true;
                    }
                    break;
                }
                case BindingSource::MouseButton:
                {
                    dshell::IMouse* mouse = devices.Mouse();
                    if (mouse != nullptr && mouse->IsButtonDown(static_cast<dshell::MouseButton>(b.code)))
                    {
                        out.value.x = b.scale * sign;
                        out.digitalDown = true;
                    }
                    break;
                }
                case BindingSource::MouseAxis:
                {
                    dshell::IMouse* mouse = devices.Mouse();
                    if (mouse != nullptr)
                    {
                        f32 v = 0.0f;
                        switch (static_cast<MouseAxisCode>(b.code))
                        {
                            case MouseAxisCode::DeltaX: v = mouse->DeltaX(); break;
                            case MouseAxisCode::DeltaY: v = mouse->DeltaY(); break;
                            case MouseAxisCode::Wheel:  v = mouse->ScrollY(); break;
                        }
                        out.value.x = v * b.scale * sign;
                    }
                    break;
                }
                case BindingSource::MouseDelta:
                {
                    dshell::IMouse* mouse = devices.Mouse();
                    if (mouse != nullptr)
                    {
                        out.value.x = mouse->DeltaX() * b.scale;
                        out.value.y = mouse->DeltaY() * b.scale * sign;
                    }
                    break;
                }
                case BindingSource::GamepadButton:
                {
                    ForEachPad(devices, b.device, [&](dshell::IGamepad& pad) {
                        if (pad.IsButtonDown(static_cast<dshell::GamepadButton>(b.code)))
                        {
                            out.value.x = b.scale * sign;
                            out.digitalDown = true;
                        }
                    });
                    break;
                }
                case BindingSource::GamepadAxis:
                {
                    ForEachPad(devices, b.device, [&](dshell::IGamepad& pad) {
                        const f32 v = ApplyDeadZone(pad.Axis(static_cast<dshell::GamepadAxis>(b.code)),
                                                    b.deadZone) * b.scale * sign;
                        if (std::fabs(v) > std::fabs(out.value.x)) { out.value.x = v; }
                    });
                    break;
                }
                case BindingSource::GamepadStick:
                {
                    const dshell::GamepadAxis axisX = static_cast<StickCode>(b.code) == StickCode::Left
                        ? dshell::GamepadAxis::LeftX : dshell::GamepadAxis::RightX;
                    const dshell::GamepadAxis axisY = static_cast<StickCode>(b.code) == StickCode::Left
                        ? dshell::GamepadAxis::LeftY : dshell::GamepadAxis::RightY;
                    ForEachPad(devices, b.device, [&](dshell::IGamepad& pad) {
                        Float2 v = ApplyCircularDeadZone(Float2{ pad.Axis(axisX), pad.Axis(axisY) },
                                                         b.deadZone);
                        v.x *= b.scale;
                        v.y *= b.scale * sign;
                        if (v.x * v.x + v.y * v.y > out.value.x * out.value.x + out.value.y * out.value.y)
                        {
                            out.value = v;
                        }
                    });
                    break;
                }
                case BindingSource::Composite2D:
                {
                    dshell::IKeyboard* keyboard = devices.Keyboard();
                    const f32 x = (KeyDown(keyboard, b.posX, 0u) ? 1.0f : 0.0f)
                                - (KeyDown(keyboard, b.negX, 0u) ? 1.0f : 0.0f);
                    const f32 y = (KeyDown(keyboard, b.posY, 0u) ? 1.0f : 0.0f)
                                - (KeyDown(keyboard, b.negY, 0u) ? 1.0f : 0.0f);
                    out.value = Float2{ x * b.scale, y * b.scale * sign };
                    if (b.normalize)
                    {
                        const f32 length = std::sqrt(out.value.x * out.value.x + out.value.y * out.value.y);
                        if (length > 1.0f) { out.value.x /= length; out.value.y /= length; }
                    }
                    out.digitalDown = x != 0.0f || y != 0.0f;
                    break;
                }
            }
            return out;
        }

        template <typename Fn>
        static void ForEachPad(IInputSourceProvider& devices, i32 wanted, Fn&& fn)
        {
            const i32 count = devices.GamepadCount();
            for (i32 i = 0; i < count; ++i)
            {
                dshell::IGamepad* pad = devices.Gamepad(i);
                if (pad == nullptr || !pad->Connected()) { continue; }
                if (wanted >= 0 && pad->Index() != wanted) { continue; }
                fn(*pad);
            }
        }

        [[nodiscard]] static f32 ApplyResponse(f32 v, f32 exponent)
        {
            if (exponent == 1.0f || v == 0.0f) { return v; }
            const f32 curved = std::pow(std::fabs(v), exponent);
            return v < 0.0f ? -curved : curved;
        }

        [[nodiscard]] static f32 MoveToward(f32 current, f32 target, f32 maxDelta)
        {
            const f32 diff = target - current;
            if (std::fabs(diff) <= maxDelta) { return target; }
            return current + (diff > 0.0f ? maxDelta : -maxDelta);
        }

        void EvaluateAction(const Action& action, ActionState& state,
                            IInputSourceProvider& devices, f32 deltaTime, bool suppressed,
                            bool latchHeld)
        {
            // Fold bindings: per-component max magnitude (Godot MAX), digital OR.
            Float2 target{ 0.0f, 0.0f };
            bool digitalDown = false;
            for (const Binding& b : action.bindings)
            {
                const Contribution c = EvaluateBinding(b, devices);
                if (std::fabs(c.value.x) > std::fabs(target.x)) { target.x = c.value.x; }
                if (std::fabs(c.value.y) > std::fabs(target.y)) { target.y = c.value.y; }
                digitalDown = digitalDown || c.digitalDown;
            }

            // Processors: response curve, then key-axis smoothing (sensitivity ramp,
            // gravity recenter, snap-on-flip). sensitivity==0 = instant.
            const ActionProcessors& proc = action.processors;
            target.x = ApplyResponse(target.x, proc.responseExponent);
            target.y = ApplyResponse(target.y, proc.responseExponent);
            if (proc.sensitivity > 0.0f && action.kind != ActionKind::Button)
            {
                auto smooth = [&](f32 current, f32 wanted) {
                    if (proc.snap && wanted != 0.0f && current != 0.0f
                        && ((wanted > 0.0f) != (current > 0.0f))) { current = 0.0f; }
                    const f32 rate = (wanted == 0.0f && proc.gravity > 0.0f) ? proc.gravity
                                                                             : proc.sensitivity;
                    return MoveToward(current, wanted, rate * deltaTime);
                };
                state.smoothed.x = smooth(state.smoothed.x, target.x);
                state.smoothed.y = smooth(state.smoothed.y, target.y);
            }
            else
            {
                state.smoothed = target;
            }

            const f32 strength = Max(std::fabs(state.smoothed.x), std::fabs(state.smoothed.y));
            const bool physicallyPressed = digitalDown || strength > kPressPoint;

            // Suppression latching (ez RequireKeyUp): a press that lives through a
            // suppression window must not re-fire when the window ends; an exclusive
            // TRANSITION latches every held action (modal boundaries demand a fresh press).
            if ((suppressed || latchHeld) && physicallyPressed) { state.latched = true; }
            if (!physicallyPressed) { state.latched = false; }
            const bool effective = physicallyPressed && !suppressed && !state.latched;

            state.value = (suppressed || state.latched) ? Float2{ 0.0f, 0.0f } : state.smoothed;
            if (effective && !state.pressed) { state.pressedFrame = m_frame; }
            if (!effective && state.pressed) { state.releasedFrame = m_frame; }
            state.pressed = effective;
        }

        InputMap m_map;
        Array<ActionState> m_states;     // flat, parallel to (set, action) in map order
        Array<u8> m_setEnabled;
        Array<String> m_exclusiveStack;
        Array<RefEntry> m_refs;
        u64 m_frame = 0;
        bool m_latchHeldOnce = false;   // set by exclusive push/pop, consumed next Update
    };
}
