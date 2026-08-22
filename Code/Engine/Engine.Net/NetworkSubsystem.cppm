/// Engine::Net - the `engine.net` module.
///
/// A Context-level subsystem (once-per-context BY CONTRACT) that integrates networking into scenes:
/// it contributes the NetworkComponentManager to the declarative scene composition (SceneModule), so
/// authoring a NetworkComponent on an entity is all a game needs for that entity to be replicable. It
/// also registers the replicated-component reflection.
///
/// This is the SCENE-INTEGRATION half of networking, deliberately separate from the per-instance
/// ENDPOINT (foundation.net.manager's NetworkManager, one per running game). The distinction: injecting
/// the component manager is a once-per-context concern (a Subsystem, like PhysicsSubsystem injecting
/// its managers); the live endpoint (server vs client) is per-GameInstance. The subsystem does no
/// per-frame work - the endpoint drives replication over the manager the subsystem installed.

module;
#include "Core/Prelude.h"

export module engine.net;

import foundation.core;
import foundation.runtime;         // Subsystem, Context
import foundation.scene; // Scene + SceneSystem
import engine.scene; // SceneSubsystem (to register as scene-aware)
import foundation.net.replication; // NetworkComponentManager + RegisterReplicationComponents
import foundation.net.manager;     // NetworkManager (the endpoint the scene system drives)

using namespace foundation::core;

export namespace engine::net
{
    namespace scene = foundation::scene;
    namespace net = foundation::net;

    // The per-scene REPLICATION driver (networking-extraction.md P2): installed into every scene by the
    // net SceneModule and ticked on the scene's FIXED lane (deterministic, physics-lockstep). It holds
    // the endpoint that replicates THIS scene - null (inert) unless this scene is the endpoint's current
    // replicated scene. The per-instance NetworkController wires/clears the endpoint at the edges
    // (SetReplicatedScene / StartServer / Connect / StopNetworking); this is a plain setter, no service.
    class NetworkSceneSystem final : public scene::SceneSystem
    {
    public:
        void SetEndpoint(net::NetworkManager* endpoint) noexcept { m_endpoint = endpoint; }
        [[nodiscard]] net::NetworkManager* Endpoint() const noexcept { return m_endpoint; }

    protected:
        void OnFixedUpdate(f32 /*fixedDeltaTime*/) override
        {
            if (m_endpoint != nullptr)
            {
                m_endpoint->UpdateReplication(); // server: capture+send; client: sample-interp+apply
            }
        }

    private:
        net::NetworkManager* m_endpoint = nullptr; // borrowed; owned by the run's NetworkController
    };

    // The net SceneModule installer (engine level): the foundation component managers PLUS the scene
    // system, so the fixed-lane replication driver ships with the net managers as ONE composition module
    // (kNetModule stays a single module - ModuleCount unchanged). Mirrors the physics/audio installer
    // shape (engine::physics::AddPhysicsSceneManagers, etc.).
    inline void AddNetworkSceneManagers(scene::Scene& scene)
    {
        net::AddNetworkSceneManagers(scene); // foundation.net.replication: identity + transform managers
        scene.AddSystem<NetworkSceneSystem>();
    }

    class NetworkSubsystem final : public foundation::runtime::Subsystem
    {
    protected:
        void OnInit() override
        {
            net::RegisterReplicationComponents(); // tooling: the reflected NetworkComponent (idempotent)
        }
    };

} // namespace engine::net
