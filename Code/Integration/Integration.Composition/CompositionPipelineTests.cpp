// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Integration.Composition - the engine composition x the pipeline registration: the join the
// scene format reference reads (a Ref<T>'s runtime type -> the factory description's cooked
// form -> the builder that produces it -> the asset type an agent authors). Cross-collection
// (Engine x Pipeline), so it lives here: the engine suites stay pipeline-free, and the web
// build, which has no pipeline, never sees it.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.resource;
import engine.composition;    // FullComposition: every factory description
import pipeline.core;         // BuilderRegistry
import pipeline.registration; // RegisterPipelineTypes + RegisterAllBuilders

using namespace foundation::core;

TEST_CASE("integration.composition: every factory the composition describes reads a cooked form that "
          "is a registered serializable some builder produces - the runtime-to-asset link the scene "
          "format reference joins")
{
    engine::RegisterAllResourceTypes();
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders{DefaultAllocator()};
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
            INFO("module: ", doctest::String(reinterpret_cast<const char*>(module.id.Data()),
                                             static_cast<unsigned>(module.id.Size())));
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
