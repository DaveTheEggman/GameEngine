// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Scene - the `:composition` partition.
///
/// The declarative assembly layer: separates "what systems a scene is made of"
/// (a static blueprint, `SceneModule` + `SceneComposition`) from "who reacts to a scene's lifecycle"
/// (`ISceneObserver` + `SceneLifecycleStage`), and centralizes the per-scene time-scale chain in
/// `FrameTime`. `SceneRegistry` is the pure, non-Subsystem state that a thin `SceneSubsystem`
/// drives - the app-wide observer list + the registered `SceneManager` list + the cross-scene
/// sweeps, testable with no `Context`.
///
/// All types are runtime-free (foundation.scene never reaches up to the runtime layer) and are built
/// on `foundation.core` value types only. `SceneModule` and `SceneComposition` are the single source of
/// truth that both the runtime path and headless scene consumers (Engine.SceneSurface transcode / MCP
/// validate) instantiate scenes from, replacing the imperative, duplicated `AddAllSceneManagers` list
/// and its `kSceneSystemCount` count tripwire.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module foundation.scene:composition;

import foundation.core;
import :scene;      // Scene (the observers/composition reference it by value)
import :manager;    // SceneManager (SceneRegistry drives managers on the lanes)
import :frame_time; // FrameTime (the lane fan-out hands it to every manager)

using namespace foundation::core;

export namespace foundation::scene
{

    // ---- Lifecycle stage + observer -----------------------------------------------------------------
    // The explicit, ordered lifecycle a scene walks. Assembly no longer happens through observation:
    // a module installs systems (SceneComposition), and an observer reacts at one of these stages.
    // NOTE deliberately NO Started/Stopped stages: they were declared in the first cut and
    // never fired anywhere (Scene::Start/Stop are invoked directly on the Scene, out of the
    // registry's sight), and domain logic already has SceneSystem::OnSceneStarted/Stopped.
    // Re-add WITH wiring if an app-level observer ever genuinely needs them (2026-08-19
    // review: ship only stages that fire).
    enum class SceneLifecycleStage : u8
    {
        Composing = 0, // a scene object exists; systems are being installed into it
        SystemsReady,  // all modules installed their systems; cross-system state is now reachable
        Destroying,    // the scene is being torn down (drop references here)
        Count,
    };

    class Scene; // defined in :scene (same module)

    // An observer reacts to ONE stage of a scene's lifecycle. Register it on a SceneRegistry with
    // AddObserver(observer, stage). Order() (lower first) breaks ties within a stage - the replacement
    // the explicit, data-driven replacement for the old injection flow.
    class ISceneObserver
    {
    public:
        virtual ~ISceneObserver() = default;

        virtual void OnComposing(Scene& /*scene*/) {}
        virtual void OnSystemsReady(Scene& /*scene*/) {}
        virtual void OnDestroying(Scene& /*scene*/) {}

        [[nodiscard]] virtual i32 Order() const noexcept { return 0; }
    };

    // ---- SceneModule: one domain's declarative "how to build a scene" -------------------------------
    // Replaces the free Add<Domain>SceneManagers + Register<Domain>ComponentReflection pair. `dependsOn`
    // (pointers to sibling modules) declares cross-domain construction order as data, not tick order.
    class SceneModule
    {
    public:
        using InstallFn = void (*)(Scene&);          // constructs managers / settings systems
        using RegisterReflectionFn = void (*)();     // registers component reflection (idempotent)

        SceneModule() = default;
        SceneModule(StringView id, InstallFn install, RegisterReflectionFn registerReflection,
                    Span<const SceneModule*> dependsOn = {})
            : m_id(id), m_install(install), m_registerReflection(registerReflection),
              m_dependsOn(dependsOn)
        {
        }

        [[nodiscard]] StringView Id() const noexcept { return m_id; }
        [[nodiscard]] Span<const SceneModule*> DependsOn() const noexcept { return m_dependsOn; }
        [[nodiscard]] bool HasInstall() const noexcept { return m_install != nullptr; }
        // Raw accessors for SceneComposition's stored-by-value copy (which strips dependsOn).
        [[nodiscard]] InstallFn InstallFunction() const noexcept { return m_install; }
        [[nodiscard]] RegisterReflectionFn RegisterReflectionFunction() const noexcept
        {
            return m_registerReflection;
        }

        void Install(Scene& scene) const
        {
            if (m_install != nullptr)
            {
                m_install(scene);
            }
        }
        void RegisterReflection() const
        {
            if (m_registerReflection != nullptr)
            {
                m_registerReflection();
            }
        }

    private:
        StringView m_id;
        InstallFn m_install = nullptr;
        RegisterReflectionFn m_registerReflection = nullptr;
        Span<const SceneModule*> m_dependsOn;
    };

    // ---- SceneModuleContributions: modules added at RUNTIME (game-native-code.md S1) ---------------
    // A game's native plugin contributes its component managers here from OnLoad; EVERY
    // SceneComposition::Instantiate appends the contributed modules after its own order (so the
    // runtime SceneSubsystem path and the headless AddAllSceneManagers path both see them), and
    // scenes already alive get them applied through the live-scene sink the SceneSubsystem
    // installs. Keyed by the installed system's TypeId (one manager type per contribution) so
    // removal can find it in live scenes. Process-single: the accessor is defined in the
    // implementation unit (shared-libraries.md rendezvous rule), and PluginHost records
    // contributions through the observer so unload reverses them.
    class SceneModuleContributions
    {
    public:
        struct Contribution
        {
            StringView id;        // borrowed literal (the plugin's)
            SceneModule::InstallFn install = nullptr;
            SceneModule::RegisterReflectionFn registerReflection = nullptr;
            TypeId systemType = 0; // TypeOf<TheManager>().id - the live-removal key
        };

        [[nodiscard]] static SceneModuleContributions& Global() noexcept;

        // Idempotent by systemType. Applies to live scenes (when a sink is installed) and
        // fires the observer only on a REAL insert.
        void Add(const Contribution& contribution);
        // Removes from live scenes first (the manager dies while its code is mapped), then
        // forgets the contribution. No-op when absent.
        void Remove(TypeId systemType);
        [[nodiscard]] bool Contains(TypeId systemType) const noexcept;
        [[nodiscard]] usize Count() const noexcept { return m_contributions.Size(); }

        // Appends every contribution's systems to `scene` (Instantiate's tail) / registers
        // their reflection (RegisterReflection's tail).
        void InstallAll(Scene& scene) const;
        void RegisterAllReflection() const;

        // The live-scene sink: visits every live scene (the SceneSubsystem installs one over
        // its registry; null = no live scenes, e.g. headless tools).
        using LiveSceneVisitor = void (*)(void* context, void (*fn)(void* fnContext, Scene&),
                                          void* fnContext);
        void SetLiveSceneSink(LiveSceneVisitor visitor, void* context) noexcept
        {
            m_liveVisitor = visitor;
            m_liveContext = context;
        }
        // After a contribution installs into a LIVE scene: the owner's chance to resolve
        // preserved component records into the new manager (Scene.Resource does that; this
        // partition is runtime- and resource-free, so it is a hook).
        using LiveInstallHook = void (*)(void* context, Scene& scene);
        void SetLiveInstallHook(LiveInstallHook hook, void* context) noexcept
        {
            m_liveInstallHook = hook;
            m_liveInstallContext = context;
        }

        // PluginHost's recording hook (one at a time; fires only on real inserts).
        void SetRegistrationObserver(void (*observer)(void*, TypeId), void* context) noexcept
        {
            m_observer = observer;
            m_observerContext = context;
        }

    private:
        void ForEachLiveScene(void (*fn)(void*, Scene&), void* fnContext) const
        {
            if (m_liveVisitor != nullptr)
            {
                m_liveVisitor(m_liveContext, fn, fnContext);
            }
        }

        Array<Contribution> m_contributions;
        LiveSceneVisitor m_liveVisitor = nullptr;
        void* m_liveContext = nullptr;
        LiveInstallHook m_liveInstallHook = nullptr;
        void* m_liveInstallContext = nullptr;
        void (*m_observer)(void*, TypeId) = nullptr;
        void* m_observerContext = nullptr;
    };

    // ---- SceneComposition: the one-time-built, topological blueprint --------------------------------
    class SceneComposition
    {
    public:
        SceneComposition() = default;

        // Builds the topologically sorted module order from `modules`. The composition COPIES each
        // module BY VALUE (a SceneModule is a StringView id + two function pointers) and drops its
        // `dependsOn` span - dependency pointers are BUILD-TIME data only, consulted here and never
        // retained. That makes the composition self-contained: callers may build from stack-local /
        // temporary module objects (the 2026-08-19 review found the pointer-retaining version
        // dereferencing a dead stack module - the exact dangling-pointer strain #5 this redesign set
        // out to remove, reintroduced; GCC caught it, clang passed on stack-layout luck).
        // A module whose dependency is absent from `modules` treats that dependency as satisfied (it
        // is out of scope for this composition). A dependency CYCLE is broken LOUDLY: the first
        // remaining module is emitted with an error log naming it, so Build always terminates and
        // contains every module exactly once - and the authoring mistake is visible, not silent.
        static SceneComposition Build(Span<const SceneModule*> modules)
        {
            SceneComposition comp;

            auto inSet = [&](const SceneModule* m) noexcept
            {
                for (const SceneModule* x : modules)
                {
                    if (x == m)
                    {
                        return true;
                    }
                }
                return false;
            };

            Array<const SceneModule*> remaining; // build-time working set (caller's pointers)
            Array<const SceneModule*> placedPtrs; // caller-pointer identity for the dep check
            for (const SceneModule* m : modules)
            {
                remaining.PushBack(m);
            }
            auto placed = [&](const SceneModule* m) noexcept
            {
                for (const SceneModule* x : placedPtrs)
                {
                    if (x == m)
                    {
                        return true;
                    }
                }
                return false;
            };

            while (!remaining.IsEmpty())
            {
                usize pick = remaining.Size(); // sentinel: none ready
                for (usize i = 0; i < remaining.Size(); ++i)
                {
                    const SceneModule* m = remaining[i];
                    bool ready = true;
                    for (const SceneModule* dep : m->DependsOn())
                    {
                        if (inSet(dep) && !placed(dep))
                        {
                            ready = false;
                            break;
                        }
                    }
                    if (ready)
                    {
                        pick = i;
                        break;
                    }
                }
                if (pick == remaining.Size())
                {
                    pick = 0; // cycle: make progress - but never silently (house rule)
                    LOG_ERROR(u8"Scene",
                              u8"scene-module dependency CYCLE - emitting '{}' out of order; fix "
                              u8"the dependsOn declarations",
                              remaining[0]->Id());
                }
                const SceneModule* src = remaining[pick];
                placedPtrs.PushBack(src);
                // Stored copy: id + the two functions. dependsOn is deliberately NOT carried
                // (build-time-only; a retained span would dangle exactly like the module ptr did).
                comp.m_order.PushBack(SceneModule{src->Id(), src->InstallFunction(),
                                                  src->RegisterReflectionFunction()});
                remaining.RemoveAt(pick);
            }
            return comp;
        }

        // Adds every module's declared systems to `scene`, in dependency order - then every
        // runtime CONTRIBUTION (a game's native managers; game-native-code.md S1).
        void Instantiate(Scene& scene) const
        {
            for (const SceneModule& m : m_order)
            {
                m.Install(scene);
            }
            SceneModuleContributions::Global().InstallAll(scene);
        }

        // Registers every module's component reflection (idempotent aggregate; call once at
        // startup) - contributions included.
        void RegisterReflection() const
        {
            for (const SceneModule& m : m_order)
            {
                m.RegisterReflection();
            }
            SceneModuleContributions::Global().RegisterAllReflection();
        }

        [[nodiscard]] usize ModuleCount() const noexcept { return m_order.Size(); }

    private:
        Array<SceneModule> m_order; // topologically sorted; OWNED copies (self-contained)
    };

    // ---- SceneRegistry: the pure scene-hardware state (testable with no Context) --------------------
    // Holds the composition + observer list + the registered SceneManager list + the cross-scene
    // sweeps. A future thin SceneSubsystem will own one and just fan the Context lanes over it.
    class SceneRegistry
    {
    public:
        SceneRegistry() = default;

        // ---- composition ----
        void SetComposition(SceneComposition composition) { m_composition = Move(composition); }
        [[nodiscard]] SceneComposition& Composition() noexcept { return m_composition; }
        [[nodiscard]] const SceneComposition& Composition() const noexcept { return m_composition; }
        void RegisterReflection() const { m_composition.RegisterReflection(); }

        // ---- observers (react to the scene lifecycle; one stage per registration) ----
        void AddObserver(ISceneObserver* observer, SceneLifecycleStage stage)
        {
            if (observer == nullptr)
            {
                return;
            }
            for (const ObserverEntry& e : m_observers)
            {
                if (e.observer == observer && e.stage == stage)
                {
                    return; // idempotent
                }
            }
            m_observers.PushBack(ObserverEntry{observer, stage});
            // Stable insertion-sort by Order() (lower first), so stage peers run in declared order.
            usize i = m_observers.Size() - 1;
            while (i > 0 && m_observers[i - 1].observer->Order() > observer->Order())
            {
                const ObserverEntry prev = m_observers[i - 1];
                m_observers[i - 1] = m_observers[i];
                m_observers[i] = prev;
                --i;
            }
        }

        // Removes every registration for `observer` (all stages).
        void RemoveObserver(ISceneObserver* observer)
        {
            for (usize i = m_observers.Size(); i-- > 0;)
            {
                if (m_observers[i].observer == observer)
                {
                    m_observers.RemoveAt(i);
                }
            }
        }

        // Fires `stage` to every observer registered for it, in Order() order.
        void Notify(SceneLifecycleStage stage, Scene& scene)
        {
            for (const ObserverEntry& e : m_observers)
            {
                if (e.stage != stage)
                {
                    continue;
                }
                switch (stage)
                {
                case SceneLifecycleStage::Composing: e.observer->OnComposing(scene); break;
                case SceneLifecycleStage::SystemsReady: e.observer->OnSystemsReady(scene); break;
                case SceneLifecycleStage::Destroying: e.observer->OnDestroying(scene); break;
                case SceneLifecycleStage::Count: break;
                }
            }
        }

        // ---- manager registry (every SceneManager ticks on the Context-driven lane) ----
        void RegisterManager(SceneManager* manager)
        {
            if (manager == nullptr)
            {
                return;
            }
            for (SceneManager* m : m_managers)
            {
                if (m == manager)
                {
                    return;
                }
            }
            m_managers.PushBack(manager);
        }
        void UnregisterManager(SceneManager* manager)
        {
            for (usize i = 0; i < m_managers.Size(); ++i)
            {
                if (m_managers[i] == manager)
                {
                    m_managers.RemoveAt(i);
                    return;
                }
            }
        }

        [[nodiscard]] usize ManagerCount() const noexcept { return m_managers.Size(); }

        template <typename Fn>
        void ForEachManager(Fn&& fn)
        {
            for (SceneManager* m : m_managers)
            {
                fn(*m);
            }
        }

        // A read-only registry sweep across every live scene in every manager (prefab rebuild, export).
        template <typename Fn>
        void ForEachScene(Fn&& fn)
        {
            for (SceneManager* m : m_managers)
            {
                m->ForEachScene([&fn](Scene& s) { fn(s); });
            }
        }

        // ---- lane drive (the fan-out a thin SceneSubsystem performs each frame) ----
        // The bridge builds ONE FrameTime (raw dt + context scale + fixed step); each manager
        // folds in its group term and each scene its own term (the chain in :frame_time).
        void BeginFrame(const FrameTime& time)
        {
            for (SceneManager* m : m_managers)
            {
                m->BeginFrame(time);
            }
        }
        void Update(const FrameTime& time)
        {
            for (SceneManager* m : m_managers)
            {
                m->Update(time);
            }
        }

    private:
        struct ObserverEntry
        {
            ISceneObserver* observer;
            SceneLifecycleStage stage;
        };

        SceneComposition m_composition;
        Array<SceneManager*> m_managers;       // borrowed; owned by pages / instances
        Array<ObserverEntry> m_observers;      // Order()-sorted within stage
    };

} // namespace foundation::scene