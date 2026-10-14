// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// An application's profiler overlay (Specs/profiler.md, P4): off until the app shows it; shown,
// it sits on the UI's global overlay layer without making the layer take input, arms the
// profiler and reads its frames; hidden, it leaves the layer and lets the profiler go.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.vfs;
import foundation.runtime;
import foundation.scene;
import engine.scene;
import foundation.profiler;
import foundation.profiler.ui;
import engine.ui;
import engine.defaultapp;

using namespace foundation::core;
namespace runtime = foundation::runtime;
namespace scene = foundation::scene;
using engine::ui::UISubsystem;

namespace
{
    foundation::vfs::IFileSystem& DataFs()
    {
        static foundation::vfs::NativeFileSystem fs(foundation::vfs::FindDataRoot(), DefaultAllocator());
        return fs;
    }
}

TEST_CASE("profiler overlay host: off until shown, then on the overlay layer, armed and reading")
{
    runtime::Context ctx(DefaultAllocator());
    auto* scenes = ctx.AddSubsystem<engine::scene::SceneSubsystem>();
    scene::SceneManager sm{DefaultAllocator()};
    scenes->RegisterManager(&sm);
    auto* ui = ctx.AddSubsystem<UISubsystem>(DefaultAllocator(), DataFs());
    ctx.Startup();

    foundation::profiler::Profiler profiler(DefaultAllocator());
    engine::runtime::ProfilerOverlayHost host(DefaultAllocator(), profiler);
    const usize before = ui->ScreenOverlayCount();
    CHECK_FALSE(host.Shown());
    CHECK(host.Overlay() == nullptr);
    CHECK_FALSE(profiler.Armed());

#if BUILD_PROFILING
    host.Toggle(*ui);
    REQUIRE(host.Shown());
    REQUIRE(host.Overlay() != nullptr);
    CHECK(ui->ScreenOverlayCount() == before + 1);
    CHECK_FALSE(ui->OverlayLayerWantsInput()); // the game under it keeps the pointer
    CHECK(profiler.Armed());

    // A frame recorded while shown reaches its views at the next refresh.
    profiler.BeginFrame();
    REQUIRE(profiler.BeginScope("Update"));
    profiler.EndScope();
    profiler.EndFrame();
    host.Refresh();
    REQUIRE(host.Overlay()->Scopes().Rows().Size() == 1);
    CHECK(host.Overlay()->Scopes().Rows()[0].name == StringView(u8"Update"));

    host.Toggle(*ui);
    CHECK_FALSE(host.Shown());
    CHECK(ui->ScreenOverlayCount() == before);
    CHECK_FALSE(profiler.Armed());

    host.Show(*ui); // shown again: the same overlay
    host.Show(*ui); // once only
    CHECK(ui->ScreenOverlayCount() == before + 1);
    host.Hide();
#else
    host.Show(*ui); // compiled out: nothing shows
    CHECK_FALSE(host.Shown());
#endif
    CHECK_FALSE(profiler.Armed());
    ctx.Shutdown();
}
