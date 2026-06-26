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
import raptor.runtime.client;   // IApplication, IApplicationHost

export namespace raptor::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        void Configure(IApplicationHost& host) override
        {
            (void)host;
            // TODO: register InputSubsystem / SceneSubsystem / RenderSubsystem / ...
            // into host.Ctx() here as those subsystems are implemented. Each new
            // engine subsystem becomes a link dependency of THIS library only.
        }
    };
}
