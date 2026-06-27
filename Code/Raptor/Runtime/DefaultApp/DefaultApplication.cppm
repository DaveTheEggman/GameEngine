// Raptor::RuntimeDefaultApp — the `raptor.runtime.defaultapp` module.
//
// DefaultApplication: an opinionated IApplication base that registers the standard
// engine subsystems. A game that wants the batteries-included engine writes
// `class MyGame : DefaultApplication` and adds its own subsystems in Configure
// (calling the base first); a game that wants only its own subsystems implements
// IApplication directly and links none of this.
//
// This lives in its OWN library — separate from raptor.runtime.client — precisely
// so the base client never pulls in the engine subsystem libraries. As the
// standard subsystems (input/scene/render/...) land, this library gains the
// dependencies; the base client stays lean. Stub for now (no subsystems exist).

module;
#include "Core/Prelude.h"

export module raptor.runtime.defaultapp;

import raptor.core;
import raptor.runtime.client;     // IApplication, IApplicationHost
import raptor.scene.subsystem;    // SceneSubsystem (the standard scene driver)

export namespace raptor::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        // Registers the standard engine subsystems. A game subclass overrides this,
        // calls DefaultApplication::Configure(host) first, then adds its own.
        void Configure(IApplicationHost& host) override
        {
            host.Ctx().AddSubsystem<raptor::scene::SceneSubsystem>();
            // TODO: InputSubsystem / RenderSubsystem / ... land here as they're built;
            // each becomes a link dependency of THIS library only.
        }
    };
}
