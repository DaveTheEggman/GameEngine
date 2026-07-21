/// Draconic::NetSubsystem - the `draconic.net.subsystem` module (docs/design/networking.md §6).
///
/// The RUNTIME HOME for networking: NetSubsystem owns a live NetSession + RpcTable over an
/// IDatagramSocket (real UDP or the sim), is driven each fixed step by the app, and installs a
/// script service so the `Net` facade can query the session and fire RPCs from Wren / AngelScript.
/// Mirrors how the ScriptSubsystem installs its runtime binding; the Net facade mirrors Time/Random.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.net.subsystem;

import draconic.core;
import draconic.net;
import draconic.script;   // Object / IScriptContext / CurrentScriptContext / SetService

using namespace draconic::core;
using namespace draconic::script;   // Object, IScriptContext, CurrentScriptContext (the facade base)

export namespace draconic::net {

// The service key the Net facade resolves per script context (distinct from script.runtime).
inline constexpr StringView kNetScriptService = u8"net.runtime";

// What the Net facade reads/acts on: the live session + its RPC table. Installed as a script service
// by NetSubsystem::InstallScriptService.
struct NetScriptBinding {
    NetSession* session = nullptr;
    RpcTable*   rpc = nullptr;
};

// Script facade: read the session's state and fire RPCs. Static methods resolve the per-context
// NetScriptBinding (same pattern as Time/Random). Bound on both backends via reflection.
class Net final : public Object {
    DRACONIC_OBJECT(Net, Object)
public:
    [[nodiscard]] static NetScriptBinding* Resolve() {
        IScriptContext* context = CurrentScriptContext();
        return context != nullptr ? static_cast<NetScriptBinding*>(context->GetService(kNetScriptService)) : nullptr;
    }

    [[nodiscard]] static bool isServer() { NetScriptBinding* b = Resolve(); return b != nullptr && b->session != nullptr && b->session->IsServer(); }
    [[nodiscard]] static bool isClient() { NetScriptBinding* b = Resolve(); return b != nullptr && b->session != nullptr && b->session->IsClient(); }
    [[nodiscard]] static i32 peerCount() { NetScriptBinding* b = Resolve(); return (b != nullptr && b->session != nullptr) ? static_cast<i32>(b->session->PeerCount()) : 0; }
    [[nodiscard]] static f64 networkTick() { NetScriptBinding* b = Resolve(); return (b != nullptr && b->session != nullptr) ? static_cast<f64>(b->session->NetworkTick()) : 0.0; }
    [[nodiscard]] static f64 networkTimeMs() { NetScriptBinding* b = Resolve(); return (b != nullptr && b->session != nullptr) ? b->session->NetworkTimeMs() : 0.0; }

    // Fire an RPC (no args). Client -> the server; server -> broadcast to all clients.
    static void rpc(String name) { CallRpc(name.AsView(), Function<void(BitWriter&)>{}); }
    // Fire an RPC with one number / one text argument (the common turn-based-order shapes).
    static void rpcNumber(String name, f64 value) {
        CallRpc(name.AsView(), [value](BitWriter& w) { u64 bits = 0; MemCopy(&bits, &value, sizeof(bits)); w.WriteU64(bits); });
    }
    static void rpcText(String name, String text) {
        String owned(text);
        CallRpc(name.AsView(), [owned](BitWriter& w) {
            w.WriteVarU32(static_cast<u32>(owned.Size()));
            w.WriteBytes(Span<const byte>(reinterpret_cast<const byte*>(owned.Data()), owned.Size()));
        });
    }

private:
    static void CallRpc(StringView name, const Function<void(BitWriter&)>& writeArgs) {
        NetScriptBinding* b = Resolve();
        if (b == nullptr || b->session == nullptr || b->rpc == nullptr) { return; }
        if (b->session->IsClient()) { b->rpc->Call(*b->session, b->session->ServerPeer(), name, writeArgs); }
        else if (b->session->IsServer()) { b->rpc->CallAll(*b->session, name, writeArgs); }
    }
};

// The runtime networking subsystem: owns the session + RPC table, driven each fixed step, and the
// installer of the Net facade's script service. App-level (one per app), not per-scene.
class NetSubsystem {
public:
    explicit NetSubsystem(IDatagramSocket& socket, const ReliableConfig& config = {})
        : m_session(socket, config) {}

    // Roles (see NetSession).
    void StartServer(bool dedicated = false) { m_session.StartServer(dedicated); }
    PeerId ConnectTo(const DatagramEndpoint& server) { return m_session.Connect(server); }

    // Drive the session + route RPCs to the table. Non-RPC events reach `onEvent` (e.g. Connected).
    void Update(f32 deltaMs, const Function<void(const NetEvent&)>& onEvent = {}) {
        m_session.Update(deltaMs);
        m_rpc.Pump(m_session, onEvent);
    }

    // Install the Net facade's service into a script context (call once, after the context exists).
    void InstallScriptService(IScriptContext& context) {
        m_binding.session = &m_session;
        m_binding.rpc = &m_rpc;
        context.SetService(kNetScriptService, &m_binding);
    }

    [[nodiscard]] NetSession& Session() noexcept { return m_session; }
    [[nodiscard]] RpcTable& Rpc() noexcept { return m_rpc; }

private:
    NetSession       m_session;
    RpcTable         m_rpc;
    NetScriptBinding m_binding;
};

// ---- runtime startup: how a host (DefaultApplication) enters a networked role from config ----

// The role a networked run starts in. None = single-player (no socket opened, no facade service).
enum class NetworkRole { None, Server, Client };

// Declarative startup config an app presets before it configures its subsystems (mirrors the
// audio-engine-settings preset). IPv4 for v1: serverHost is a dotted-quad (no DNS yet).
struct NetworkStartup {
    NetworkRole role = NetworkRole::None;
    u16 listenPort = 0;                         // server: bind port; client: 0 = OS-assigned
    core::String serverHost = core::String(u8"127.0.0.1");  // client: server address to connect to
    u16 serverPort = 0;                         // client: the server's port
    bool dedicated = false;                     // server: dedicated (no local player) vs listen-server
    ReliableConfig reliable = {};               // protocol tuning (keepalive/timeout/resend)
};

// A started net home: the socket the subsystem borrows + the subsystem itself. Both must outlive
// the run; the subsystem holds a reference to the socket, so keep/destroy the subsystem FIRST.
// socket/subsystem are null when role==None; a non-null socket that failed to open reports
// !IsOpen() (the caller logs). Bundled so the socket-open + role-entry logic stays in this lib.
struct NetworkRuntime {
    core::UniquePtr<UdpSocket>     socket;
    core::UniquePtr<NetSubsystem> subsystem;

    [[nodiscard]] bool IsActive() const noexcept { return subsystem.Get() != nullptr; }
};

// Open the socket, build the subsystem, and enter the role (StartServer / ConnectTo). Returns an
// empty NetworkRuntime for role==None. Registers the Net script facade as a side effect when a
// role is entered (idempotent), so the facade is bound wherever networking is actually used.
[[nodiscard]] NetworkRuntime StartNetworking(const NetworkStartup& config);

/// Register the Net facade type (call before a script manager is created, like
/// RegisterScriptFacadeReflection). Idempotent.
void RegisterNetScriptFacade();

}
