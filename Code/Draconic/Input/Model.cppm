// Draconic::Input - :model partition.
//
// The action-mapping DATA MODEL (docs/design/input.md §3.1): one InputMap = a whole game's
// bindings - ActionSets (contexts with priority) of Actions (declared kinds, never inferred)
// of Bindings (a tagged flat record covering every physical source; flat = trivially
// serializable and editor-grid friendly). Per-action processors carry the Flax-style key-axis
// smoothing and ez's response-curve/time-scale properties. Pure data + serialization - the
// evaluation lives in :runtime.

module;
#include "Core/Prelude.h"

export module draconic.input:model;

import draconic.core;

using namespace draconic::core;

export namespace draconic::input
{
    // Declared, never inferred (Sedulous's collapsed {X,Y} was lossy): querying a Button as a
    // vector is a caller error surfaced by the editor's validation, not a silent 0.
    enum class ActionKind : u8
    {
        Button,
        Axis1D,
        Axis2D,
    };

    enum class BindingSource : u8
    {
        Key,             // code = shell::KeyCode; optional modifier mask
        MouseButton,     // code = shell::MouseButton
        MouseAxis,       // Axis1D rate: code = MouseAxisCode; NEVER time-scaled (ez rule)
        MouseDelta,      // Axis2D rate: (dx, dy)
        GamepadButton,   // code = shell::GamepadButton; device -1 = any
        GamepadAxis,     // Axis1D: code = shell::GamepadAxis; deadZone/invert/scale
        GamepadStick,    // Axis2D: code = StickCode; circular dead zone
        Composite2D,     // Axis2D from four digital keys (WASD); normalize flag
    };

    enum class MouseAxisCode : u32 { DeltaX = 0, DeltaY = 1, Wheel = 2 };
    enum class StickCode : u32 { Left = 0, Right = 1 };

    // One physical binding, flat + tagged: only the fields the source uses are meaningful,
    // the rest stay at defaults (they serialize compactly and the editor grid hides them).
    struct Binding
    {
        BindingSource source = BindingSource::Key;
        u32 code = 0;            // KeyCode / MouseButton / MouseAxisCode / GamepadButton / GamepadAxis / StickCode
        u32 modifiers = 0;       // Key only: required shell::KeyModifiers mask (0 = none required)
        i32 device = -1;         // gamepad index; -1 = any connected pad
        f32 deadZone = 0.15f;    // analog sources; circular for sticks
        f32 scale = 1.0f;        // response scale (a key bound to an axis uses -1 for the negative direction)
        bool invert = false;     // flips the value (sticks: flips Y)
        bool normalize = true;   // Composite2D: clamp diagonal length to 1
        u32 negX = 0;            // Composite2D key codes
        u32 posX = 0;
        u32 negY = 0;
        u32 posY = 0;
    };

    // Per-action value conditioning (applied to the folded target each frame).
    struct ActionProcessors
    {
        f32 sensitivity = 0.0f;      // >0: key-driven axes RAMP toward the target at this rate/sec (Flax)
        f32 gravity = 0.0f;          // >0: recenter rate/sec when the target is 0 (else sensitivity)
        bool snap = false;           // zero first on direction flip (Flax)
        f32 responseExponent = 1.0f; // analog curve: sign(v)*|v|^e (ez)
        bool timeScale = false;      // value multiplies by a global time scale when one exists (ez;
                                     // stored now, applied when the engine grows a time-scale system)
    };

    struct Action
    {
        String name;
        ActionKind kind = ActionKind::Button;
        Array<Binding> bindings;
        ActionProcessors processors;
    };

    // A context: "Gameplay" / "Menu" / "Vehicle". Priority orders QUERY resolution when the
    // same action name exists in several sets (higher wins among enabled sets).
    struct ActionSet
    {
        String name;
        i32 priority = 0;
        Array<Action> actions;
    };

    struct InputMap
    {
        Array<ActionSet> sets;
    };

    // ---- serialization (shared by the source asset and the cooked resource) ----

    inline constexpr u32 kInputMapVersion = 1;

    inline void SerializeBinding(ISerializer& ar, Binding& b)
    {
        u8 source = static_cast<u8>(b.source);
        draconic::core::Serialize(ar, "source", source);
        b.source = static_cast<BindingSource>(source);
        draconic::core::Serialize(ar, "code", b.code);
        draconic::core::Serialize(ar, "modifiers", b.modifiers);
        draconic::core::Serialize(ar, "device", b.device);
        draconic::core::Serialize(ar, "deadZone", b.deadZone);
        draconic::core::Serialize(ar, "scale", b.scale);
        draconic::core::Serialize(ar, "invert", b.invert);
        draconic::core::Serialize(ar, "normalize", b.normalize);
        draconic::core::Serialize(ar, "negX", b.negX);
        draconic::core::Serialize(ar, "posX", b.posX);
        draconic::core::Serialize(ar, "negY", b.negY);
        draconic::core::Serialize(ar, "posY", b.posY);
    }

    inline void SerializeInputMap(ISerializer& ar, InputMap& map)
    {
        const bool writing = ar.Mode() == SerializeMode::Write;
        u32 version = kInputMapVersion;
        draconic::core::Serialize(ar, "version", version);

        u32 setCount = writing ? static_cast<u32>(map.sets.Size()) : 0;
        ar.Key("sets");
        ar.BeginArray(setCount);
        if (!writing) { map.sets.Clear(); map.sets.Resize(setCount); }
        for (u32 s = 0; s < setCount; ++s)
        {
            ActionSet& set = map.sets[s];
            draconic::core::Serialize(ar, "name", set.name);
            draconic::core::Serialize(ar, "priority", set.priority);
            u32 actionCount = writing ? static_cast<u32>(set.actions.Size()) : 0;
            ar.Key("actions");
            ar.BeginArray(actionCount);
            if (!writing) { set.actions.Resize(actionCount); }
            for (u32 a = 0; a < actionCount; ++a)
            {
                Action& action = set.actions[a];
                draconic::core::Serialize(ar, "name", action.name);
                u8 kind = static_cast<u8>(action.kind);
                draconic::core::Serialize(ar, "kind", kind);
                action.kind = static_cast<ActionKind>(kind);
                draconic::core::Serialize(ar, "sensitivity", action.processors.sensitivity);
                draconic::core::Serialize(ar, "gravity", action.processors.gravity);
                draconic::core::Serialize(ar, "snap", action.processors.snap);
                draconic::core::Serialize(ar, "responseExponent", action.processors.responseExponent);
                draconic::core::Serialize(ar, "timeScale", action.processors.timeScale);
                u32 bindingCount = writing ? static_cast<u32>(action.bindings.Size()) : 0;
                ar.Key("bindings");
                ar.BeginArray(bindingCount);
                if (!writing) { action.bindings.Resize(bindingCount); }
                for (u32 b = 0; b < bindingCount; ++b)
                {
                    SerializeBinding(ar, action.bindings[b]);
                }
                ar.EndArray();
            }
            ar.EndArray();
        }
        ar.EndArray();
    }

    // ---- validation (asset save + cook share it) ----
    // Kind mismatches are DATA errors: surfaced here, not silently zeroed at runtime.
    [[nodiscard]] inline bool ValidateInputMap(const InputMap& map, String* firstError = nullptr)
    {
        auto fail = [&](StringView message) {
            if (firstError != nullptr) { *firstError = String(message); }
            return false;
        };
        for (const ActionSet& set : map.sets)
        {
            if (set.name.IsEmpty()) { return fail(u8"action set with an empty name"); }
            for (const Action& action : set.actions)
            {
                if (action.name.IsEmpty()) { return fail(u8"action with an empty name"); }
                for (const Binding& b : action.bindings)
                {
                    const bool is2D = b.source == BindingSource::GamepadStick
                                   || b.source == BindingSource::Composite2D
                                   || b.source == BindingSource::MouseDelta;
                    const bool isAxis = b.source == BindingSource::MouseAxis
                                     || b.source == BindingSource::GamepadAxis;
                    switch (action.kind)
                    {
                        case ActionKind::Button:
                            if (is2D || isAxis) { return fail(u8"analog binding on a Button action"); }
                            break;
                        case ActionKind::Axis1D:
                            if (is2D) { return fail(u8"2D binding on an Axis1D action"); }
                            break;
                        case ActionKind::Axis2D:
                            if (!is2D) { return fail(u8"non-2D binding on an Axis2D action"); }
                            break;
                    }
                }
            }
        }
        return true;
    }
}
