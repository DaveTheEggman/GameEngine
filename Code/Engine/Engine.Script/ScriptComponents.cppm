// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Script - :components partition.
//
// The attachment model: ONE ScriptComponent per entity
// holding an ORDERED array of behaviors - each a cooked ScriptClass reference, an
// enabled flag, and hash-keyed property OVERRIDES (values differing from the class's
// harvested defaults; Godot's default-diff semantics with Lumix's rename-safe hashes).
// Runtime fields (the live ScriptObject instance, dispatch state) are transient -
// never serialized. Because overrides live in the component payload, the prefab
// delta machinery (baseline blob compare) covers them with zero new code.

module;
#include "Core/Prelude.h"

export module engine.script:components;

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.script;
import foundation.script.resource;

using namespace foundation::core;
using namespace foundation::script;

export namespace engine::script
{
    struct ScriptPropertyOverride
    {
        u64 nameHash = 0; // ScriptPropertyNameHash of the property's name
        ScriptPropertyValue value;
    };

    inline void Serialize(ISerializer& ar, ScriptPropertyOverride& o)
    {
        foundation::core::Serialize(ar, "nameHash", o.nameHash);
        foundation::core::Serialize(ar, "value", o.value);
    }

    struct ScriptBehavior
    {
        // Authored:
        foundation::resource::Ref<ScriptClass> script;
        bool enabled = true;
        f32 updateInterval = 0.0f; // seconds between onUpdate calls; <=0 = every tick.
                                   // The delivered dt is the ACCUMULATED time.
        Array<ScriptPropertyOverride> overrides;

        // Runtime (transient):
        RefPtr<ScriptObject> instance;
        const ScriptClass* boundClass = nullptr; // product the instance was built from
                                                 // (a reload swaps the product -> re-instantiate)
        bool started = false;                    // onStart delivered
        bool active = false;          // last delivered enable state (onEnable/onDisable edges)
        bool entitySuspended = false; // entity-active latch: frozen by an inactive entity
                                      // (runtime only)
        bool faulted = false;         // a fault disables the one behavior (cleared by reload)
        f32 updateAccumulator = 0.0f; // time banked toward the next throttled onUpdate

        [[nodiscard]] const ScriptPropertyOverride* FindOverride(u64 nameHash) const
        {
            for (const ScriptPropertyOverride& entry : overrides)
            {
                if (entry.nameHash == nameHash)
                {
                    return &entry;
                }
            }
            return nullptr;
        }
        void SetOverride(u64 nameHash, const ScriptPropertyValue& value)
        {
            for (ScriptPropertyOverride& entry : overrides)
            {
                if (entry.nameHash == nameHash)
                {
                    entry.value = value;
                    return;
                }
            }
            overrides.PushBack(ScriptPropertyOverride{nameHash, value});
        }
        void RemoveOverride(u64 nameHash)
        {
            for (usize i = 0; i < overrides.Size(); ++i)
            {
                if (overrides[i].nameHash == nameHash)
                {
                    overrides.RemoveAt(i);
                    return;
                }
            }
        }
    };

    inline void Serialize(ISerializer& ar, ScriptBehavior& b)
    {
        foundation::core::Serialize(ar, "script", b.script);
        foundation::core::Serialize(ar, "enabled", b.enabled);
        foundation::core::Serialize(ar, "updateInterval", b.updateInterval);
        foundation::core::Serialize(ar, "overrides", b.overrides); // count-prefixed array scope
    }

    struct ScriptComponent
    {
        Array<ScriptBehavior> behaviors; // execution order = array order
    };

    inline void Serialize(ISerializer& ar, ScriptComponent& c)
    {
        foundation::core::Serialize(ar, "behaviors", c.behaviors);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager, ScriptComponent& c)
    {
        for (ScriptBehavior& behavior : c.behaviors)
        {
            behavior.script.Bind(manager);
        }
    }

    class ScriptSceneSystem; // forward (:subsystem) - the destroy hook dispatches onDestroy

    class ScriptComponentManager final
        : public foundation::scene::SerializableComponentManager<ScriptComponent>
    {
    public:
        ScriptComponentManager() : SerializableComponentManager<ScriptComponent>(u8"script") {}

        /// The per-scene script system, wired by the subsystem right after AddSystem -
        /// entity/component destruction routes onDestroy through it.
        void SetScriptSystem(ScriptSceneSystem* system) noexcept { m_scriptSystem = system; }
        [[nodiscard]] ScriptSceneSystem* ScriptSystem() const noexcept { return m_scriptSystem; }

    protected:
        // Defined in the :subsystem partition's implementation (needs ScriptSceneSystem).
        void OnComponentDestroyed(ScriptComponent& component,
                                  foundation::scene::EntityHandle entity) override;

    private:
        ScriptSceneSystem* m_scriptSystem = nullptr;
    };

    // Defined in SubsystemImpl.cpp (REFLECT_* bodies never sit in a module
    // interface unit - the GCC gcm-cluster rule).
    void RegisterScriptComponentReflection();
}
