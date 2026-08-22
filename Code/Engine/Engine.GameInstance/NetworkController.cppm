// Engine::GameInstance :networkcontroller partition - a running game's networking, extracted off the
// GameInstance god object (networking-extraction.md P1). It OWNS what GameInstance used to BE: the
// endpoint, the INetworkController implementation, the role lifecycle (StartServer/Connect/Stop), the
// net script binding, and the replicated-scene selection. GameInstance now COMPOSES one and forwards;
// it no longer inherits net::INetworkController.
//
// This is a pure ROLE extraction (P1): the wire protocol, the replication model, and the endpoint
// factories are unchanged - the bodies moved here verbatim. Lives in engine.gameinstance (beside its
// owner); it can migrate to engine.net later if the script surface wants NetworkController.of(context).

export module engine.gameinstance:networkcontroller;

import foundation.core;
import foundation.scene;        // scene::Scene (the replicated scene) + GetSystem
import foundation.net.manager;  // NetworkManager + INetworkController + NetScriptBinding
import foundation.script;       // IScriptContext (InstallNetScriptService)
import engine.net;              // NetworkSceneSystem (the per-scene fixed-lane replication driver)

using namespace foundation::core;

export namespace engine::runtime
{
    namespace core = foundation::core;
    namespace net = foundation::net;
    namespace scene = foundation::scene;
    namespace script = foundation::script;

    // A hook the app sets once and the controller fires on every go-online, passing the freshly created
    // endpoint. The app uses it to wire per-endpoint setup that needs app state (e.g. the prefab
    // net-spawn resolver, which needs the content DB) - fresh each time, so reconnect stays correct.
    using EndpointOnlineHook = core::Function<void(net::NetworkManager&)>;

    // A running game's networking. Owns the endpoint (null = offline), implements INetworkController for
    // the Net facade, and caches the current scene so a freshly created endpoint replicates it. The
    // controller outlives every endpoint it creates, so the script binding (controller=this) never
    // dangles. Stable member of GameInstance (constructed/destructed with it).
    class NetworkController final : public net::INetworkController
    {
    public:
        /// The scene this run replicates. Cached so StartServer/Connect can set it on a fresh endpoint;
        /// applied live when the endpoint already exists (GameInstance::SetScene calls this on change).
        /// Edge-driven wiring (networking-extraction.md P2): the OLD replicated scene's NetworkSceneSystem
        /// is detached (endpoint -> null) and the NEW one attached (endpoint -> live/offline), so ONLY the
        /// replicated scene's fixed lane ever drives this endpoint's replication.
        void SetReplicatedScene(scene::Scene* scene)
        {
            if (scene == m_scene)
            {
                return; // unchanged - the endpoint + scene-system wiring are already correct
            }
            WireSceneSystem(nullptr); // detach the OLD replicated scene's system (uses current m_scene)
            m_scene = scene;
            if (m_net)
            {
                m_net->SetReplicatedScene(scene);
            }
            WireSceneSystem(m_net.Get()); // attach the NEW scene's system (live endpoint, or null offline)
        }

        /// App-set hook fired (with the live endpoint) on each go-online, so the app can wire
        /// per-endpoint setup that needs app state (the net-spawn resolver from the content DB). Not
        /// consumed - reconnect re-runs it.
        void SetEndpointOnlineHook(EndpointOnlineHook hook)
        {
            m_onEndpointOnline = static_cast<EndpointOnlineHook&&>(hook);
        }

        /// Point `context` at this controller's net binding (controller=this), so the Net facade resolves
        /// THIS run's controller. Idempotent; null-safe (no-op when the context is null).
        void InstallScriptBinding(script::IScriptContext* context)
        {
            m_binding.controller = this; // stable; the endpoint m_net points at may come and go
            if (context != nullptr)
            {
                net::InstallNetScriptService(*context, m_binding);
            }
        }

        /// Drive this run's networking on the FIXED lane (deterministic step): pump the socket, route
        /// RPCs + replication, push per-peer deltas (server) / sample interpolation (client). No-op when
        /// offline.
        void DriveNetwork(f32 fixedDeltaMs);

        // INetworkController - the Net facade calls these. StartServer/Connect open a real UDP socket and
        // enter the role (returning false if it fails); StopNetworking drops the endpoint. The live
        // endpoint replicates this run's current scene.
        bool StartServer(u16 port, bool dedicated) override;
        bool Connect(core::StringView host, u16 port) override;
        void StopNetworking() override;
        [[nodiscard]] net::NetworkManager* NetEndpoint() const override { return m_net.Get(); }

    private:
        /// Point the CURRENT replicated scene's NetworkSceneSystem at `endpoint` (its live endpoint when
        /// attaching, null when detaching / offline). No-op when there is no scene, or a scene built
        /// without the net module (no NetworkSceneSystem). The scene system dies with its scene, so this
        /// is only ever called while m_scene is alive (the controller clears m_scene before a scene dies).
        void WireSceneSystem(net::NetworkManager* endpoint)
        {
            if (m_scene == nullptr)
            {
                return;
            }
            if (engine::net::NetworkSceneSystem* system =
                    m_scene->GetSystem<engine::net::NetworkSceneSystem>())
            {
                system->SetEndpoint(endpoint);
            }
        }

        core::UniquePtr<net::NetworkManager> m_net; // this run's endpoint (null = offline)
        net::NetScriptBinding m_binding;            // stable; the facade resolves controller=this
        EndpointOnlineHook m_onEndpointOnline;      // app-set; fires with m_net on each go-online
        scene::Scene* m_scene = nullptr;            // the scene a fresh endpoint replicates
    };
}
