// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The runtime's factory set is the engine composition's (engine-composition.md D6): attaching a
// manager registers every factory the composition describes and this host's services allow.
// Incident 2026-08-12: FontFactory existed and was tested, but NO host ever registered it -
// Bind<Font> failed silently in every runtime, masked in the editor by the dev-tree TTF
// fallback, and only visible in a dist export. The pins below keep that class of failure loud;
// the count is the composition's, so a factory a domain declares can no longer be left behind.

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
import engine.composition; // FullComposition: the set the runtime composes from
#if OPTION_HAS_PIPELINE // the pipeline is tooling: absent from the web build
import pipeline.core;         // BuilderRegistry
import pipeline.registration; // RegisterPipelineTypes + RegisterAllBuilders
#endif

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

TEST_CASE("defaultapp: attaching a manager registers the composition's headless factory set, with "
          "the font and terrain pins")
{
    foundation::vfs::NativeFileSystem mount(u8"scratch_defaultapp_factories", foundation::core::DefaultAllocator());
    foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::xml::XmlSerializerFactory(),
                                            u8".xasset");
    foundation::resource::ResourceManager resources(foundation::core::DefaultAllocator(), db, nullptr);
    StubHost host;

    engine::runtime::DefaultApplication app;
    app.AttachResourceManager(&resources, host);

    // The runtime's set IS the composition's headless set: what a host with no device and no
    // shader system can create. A factory a domain declares is in both by construction.
    foundation::resource::ResourceFactorySet headless;
    foundation::resource::NoResourceServices none;
    engine::FullComposition().CreateFactories(headless, foundation::core::DefaultAllocator(), none);
    CHECK(resources.FactoryCount() == headless.Count());
    CHECK(resources.FactoryCount() == engine::FullComposition().FactoryDescriptionCount() - 2u);
    headless.ForEach([&](const foundation::resource::IResourceFactory& factory)
                     { CHECK(resources.HasFactory(factory.ProductType()->id)); });

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

#if OPTION_HAS_PIPELINE
TEST_CASE("defaultapp: every factory the composition describes reads a cooked form that is a "
          "registered serializable some builder produces - the runtime-to-asset link the scene "
          "format reference joins")
{
    engine::RegisterAllResourceTypes();
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders{foundation::core::DefaultAllocator()};
    pipeline::RegisterAllBuilders(builders);
    REQUIRE(builders.Count() == pipeline::kBuilderCount);

    usize checked = 0;
    engine::FullComposition().ForEachFactoryDescription(
        [&](const foundation::resource::ResourceModule& module,
            const foundation::resource::ResourceFactoryDesc& desc)
        {
            const TypeInfo* product = desc.product();
            const TypeInfo* cooked = desc.cooked();
            REQUIRE(product != nullptr);
            INFO("module: ", doctest::String(reinterpret_cast<const char*>(module.id.Data()), static_cast<unsigned>(module.id.Size())));
            INFO("factory for: ", doctest::String(product->name != nullptr ? product->name : "<unnamed>"));
            REQUIRE(cooked != nullptr);
            // The cooked form is what the cook stamped and ReadObject reconstructs: registered.
            CHECK(GlobalSerializableRegistry().Contains(cooked->id));
            // And some builder produces exactly it - the link from the runtime type to the asset.
            bool produced = false;
            builders.ForEach([&](const pipeline::IAssetBuilder& builder)
                             { produced = produced || builder.ProductType() == cooked; });
            INFO("cooked form: ", doctest::String(cooked->name != nullptr ? cooked->name : "<unnamed>"));
            CHECK(produced);
            ++checked;
        });
    CHECK(checked == engine::FullComposition().FactoryDescriptionCount());
}
#endif // OPTION_HAS_PIPELINE
