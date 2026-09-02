// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The RegisterStandardFactories COVERAGE TRIPWIRE (Pipeline.Registration pattern applied to the
// runtime's factory composition root). Incident 2026-08-12: FontFactory existed and was tested,
// but NO host ever registered it - Bind<Font> failed silently in every runtime, masked in the
// editor by the dev-tree TTF fallback, and only visible in a dist export (no source tree). A
// count + the specific regression pin make the next missing registration fail HERE, loudly.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.runtime;
import foundation.runtime.client;
import foundation.resource;
import foundation.content;
import foundation.shell;
import foundation.graphics;
import foundation.vfs;
import foundation.xml.serialization;
import foundation.fonts.resource;
import foundation.texture.resource;
import foundation.heightfield;          // Heightfield product type (terrain factory pins)
import foundation.terrain.resource;     // TerrainResource + Splatmap product types
import engine.defaultapp;

using namespace foundation::core;
namespace runtime = foundation::runtime;

namespace
{
    // Headless host stub: no shell, no graphics, no windows - exactly the shape a CLI/test
    // process presents. Null Graphics() also exercises the texture-factory device gating.
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
        runtime::Context m_context{DefaultAllocator()};
    };
}

TEST_CASE("defaultapp: the standard factory set is complete (count tripwire + the font pin)")
{
    foundation::vfs::NativeFileSystem mount(u8"scratch_defaultapp_factories", foundation::core::DefaultAllocator());
    foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::xml::XmlSerializerFactory(),
                                            u8".xasset");
    foundation::resource::ResourceManager resources(foundation::core::DefaultAllocator(), db, nullptr);
    StubHost host;

    engine::runtime::DefaultApplication app;
    app.AttachResourceManager(&resources, host);

    // COUNT TRIPWIRE: the standard headless set (no graphics device -> no texture factory).
    // A new standard factory bumps this constant DELIBERATELY; a lost registration fails
    // loudly here instead of as a silent null Bind in a production game.
    // 19 = +PropertyAnimationClipFactory (the editor's clip picker bind warned
    // "host is missing an AddFactory" - the factory existed but no host registered it).
    // 20 = +NavigationZoneFactory (the bump was MISSED in that change and
    // caught by this tripwire - run the FULL battery, not
    // just the touched targets).
    // 23 = +Heightfield/Terrain/Splatmap factories (creating a Terrain in the
    // editor warned "host is missing an AddFactory for TerrainResource" - the factories existed +
    // were pipeline-tested, but no host registered them; the SAME incident class as the font one).
    constexpr usize kStandardHeadlessFactoryCount = 23;
    CHECK(resources.FactoryCount() == kStandardHeadlessFactoryCount);

    // The incident pin: the cooked default-UI font product MUST be constructible in every
    // runtime host - a dist player has no dev-tree TTF fallback.
    CHECK(resources.HasFactory(foundation::fonts::Font::StaticType().id));

    // Terrain pins: a cooked Terrain binds its whole CPU ref chain (bundle + grid + splat raster).
    CHECK(resources.HasFactory(foundation::terrain::TerrainResource::StaticType().id));
    CHECK(resources.HasFactory(foundation::heightfield::Heightfield::StaticType().id));
    CHECK(resources.HasFactory(foundation::terrain::SplatWeights::StaticType().id));

    // Device gating documented: no GraphicsDevice on the host means no texture factory.
    CHECK(!resources.HasFactory(foundation::texture::Texture::StaticType().id));
}
