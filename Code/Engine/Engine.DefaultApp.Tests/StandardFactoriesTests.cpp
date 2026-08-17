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
        runtime::Context m_context;
    };
}

TEST_CASE("defaultapp: the standard factory set is complete (count tripwire + the font pin)")
{
    foundation::vfs::NativeFileSystem mount(u8"scratch_defaultapp_factories");
    foundation::content::ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(),
                                            u8".xasset");
    foundation::resource::ResourceManager resources(db, nullptr);
    StubHost host;

    engine::runtime::DefaultApplication app;
    app.AttachResourceManager(&resources, host);

    // COUNT TRIPWIRE: the standard headless set (no graphics device -> no texture factory).
    // A new standard factory bumps this constant DELIBERATELY; a lost registration fails
    // loudly here instead of as a silent null Bind in a shipped game.
    // 19 = +PropertyAnimationClipFactory (2026-08-17: the editor's clip picker bind warned
    // "host is missing an AddFactory" - the factory existed but no host registered it).
    constexpr usize kStandardHeadlessFactoryCount = 19;
    CHECK(resources.FactoryCount() == kStandardHeadlessFactoryCount);

    // The incident pin: the cooked default-UI font product MUST be constructible in every
    // runtime host - a dist player has no dev-tree TTF fallback.
    CHECK(resources.HasFactory(foundation::fonts::Font::StaticType().id));

    // Device gating documented: no GraphicsDevice on the host means no texture factory.
    CHECK(!resources.HasFactory(foundation::texture::Texture::StaticType().id));
}
