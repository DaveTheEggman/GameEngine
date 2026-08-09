/// Draconic::NetworkSubsystem - the `engine.net` module.
///
/// A Context-level subsystem (once-per-context BY CONTRACT) that integrates networking into scenes:
/// it injects the NetworkComponentManager into every scene (via ISceneAware), so authoring a
/// NetworkComponent on an entity is all a game needs for that entity to be replicable. It also
/// registers the replicated-component reflection.
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
import foundation.scene;           // Scene, ISceneAware
import engine.scene; // SceneSubsystem (to register as scene-aware)
import foundation.net.replication; // NetworkComponentManager + RegisterReplicationComponents

using namespace foundation::net;

export namespace engine::net
{

    class NetworkSubsystem final : public foundation::runtime::Subsystem,
                                   public foundation::scene::ISceneAware
    {
    public:
        // Inject the NetworkComponent manager into each new scene so authored NetworkComponents (and
        // the server's runtime AssignNetworkId) have a home. Replicated-state component managers (e.g.
        // the transform) are injected by their own subsystems - this adds only the identity tag pool.
        void OnSceneCreated(foundation::scene::Scene& scene) override
        {
            scene.AddSystem<NetworkComponentManager>(); // identity (NetworkId + authority + prefab)
            scene.AddSystem<
                NetworkedTransformComponentManager>(); // replicated transform (the common case)
        }

    protected:
        void OnInit() override
        {
            RegisterReplicationComponents(); // tooling: the reflected NetworkComponent (idempotent)
        }

        void OnReady() override
        {
            if (foundation::runtime::Context* ctx = GetContext())
            {
                if (auto* scenes = ctx->GetSubsystem<engine::scene::SceneSubsystem>())
                {
                    scenes->RegisterSceneAware(this);
                }
            }
        }

        void OnShutdown() override
        {
            if (foundation::runtime::Context* ctx = GetContext())
            {
                if (auto* scenes = ctx->GetSubsystem<engine::scene::SceneSubsystem>())
                {
                    scenes->UnregisterSceneAware(this);
                }
            }
        }
    };

} // namespace foundation::net
