// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The RUNTIME-path facade-surface tripwire (sibling of StandardFactoriesTests, same incident
// class). Incident 2026-08-22: RegisterAllScriptFacades existed and was tested, but the runtime
// app kept a hand-rolled per-subsystem registration list that drifted past RegisterRunScriptFacade
// - game scripts compiled against a prelude with no `run`, masked in editor/PIE (the editor calls
// the root) and only visible in an exported game. Configure now calls the root; these tests pin
// that the RUNTIME path yields the complete surface, with `run` as the specific regression pin.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.runtime;
import foundation.runtime.client;
import foundation.shell;
import foundation.graphics;
import foundation.script.facades;
import engine.scriptsurface;
import engine.defaultapp;

using namespace foundation::core;
namespace runtime = foundation::runtime;

namespace
{
    // Headless host stub (the StandardFactoriesTests shape): no shell, no graphics, no windows.
    // Configure guards on null Graphics()/Shell(), so the full subsystem set wires headless.
    class StubHost final : public runtime::IApplicationHost
    {
    public:
        runtime::Context& Ctx() noexcept override { return m_context; }
        foundation::shell::IShell* Shell() noexcept override { return nullptr; }
        foundation::graphics::GraphicsDevice* Graphics() noexcept override { return nullptr; }
        foundation::graphics::RenderWindow* MainRenderWindow() noexcept override
        {
            return nullptr;
        }
        foundation::graphics::RenderWindow*
        OpenWindow(const foundation::shell::WindowSettings&,
                   const foundation::graphics::RenderWindowDesc&) override
        {
            return nullptr;
        }
        void CloseWindow(foundation::graphics::RenderWindow*) override {}
        void RequestExit(int) override {}

    private:
        runtime::Context m_context;
    };

    [[nodiscard]] bool SurfaceHasName(StringView name)
    {
        for (StringView entry : foundation::script::ExtraFacadeNames())
        {
            if (entry == name)
            {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("defaultapp: Configure registers the COMPLETE facade surface (the run pin)")
{
    StubHost host;
    engine::runtime::DefaultApplication app;
    app.Configure(host);

    // The count tripwire, now measured on the RUNTIME path: Configure must yield exactly the
    // surface root's set (a facade registered anywhere else without the root would diverge here).
    CHECK(foundation::script::ExtraFacadeNames().Size() == engine::kSubsystemFacadeNameCount);

    // The specific regression pin: the exported game's `run::loadScene` gap.
    CHECK(SurfaceHasName(u8"run"));
}
