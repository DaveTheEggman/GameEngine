// Pipeline.Registration.Tests - the drift tripwire.
//
// The composition root is the single source of truth ONLY if it stays complete. These tests
// register into FRESH local registries and assert the counts against the interface's explicit
// constants: a new builder/importer bumps the constant deliberately, a lost or double
// registration fails loudly. This is the check that "the three inline copies stay in sync" never
// had - now the one copy is machine-verified.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;

using namespace foundation::core;

TEST_CASE("pipeline.registration: RegisterAllBuilders populates exactly kBuilderCount builders")
{
    pipeline::BuilderRegistry registry;
    CHECK(registry.Count() == 0u);
    pipeline::RegisterAllBuilders(registry);
    CHECK(registry.Count() == pipeline::kBuilderCount);
}

TEST_CASE("pipeline.registration: RegisterAllImporters populates exactly kImporterCount importers")
{
    pipeline::ImporterRegistry registry;
    CHECK(registry.Count() == 0u);
    pipeline::RegisterAllImporters(registry);
    CHECK(registry.Count() == pipeline::kImporterCount);
}

TEST_CASE("pipeline.registration: RegisterPipelineTypes runs the whole set without faulting")
{
    // Smoke: every registrar the headless cook needs fires once, in order, on a real process.
    // (No count to assert - this guards that the composition root's type/backend/cook
    // registrations all resolve and none abort.)
    pipeline::RegisterPipelineTypes();

    // Routing still works after registration: a fresh builder set resolves by asset type name.
    pipeline::BuilderRegistry registry;
    pipeline::RegisterAllBuilders(registry);
    CHECK(registry.Count() == pipeline::kBuilderCount);
}

namespace
{
    // True when the null-terminated `ns` begins with `prefix`.
    bool NamespaceStartsWith(const char* ns, const char* prefix)
    {
        if (ns == nullptr)
        {
            return false;
        }
        for (usize i = 0; prefix[i] != '\0'; ++i)
        {
            if (ns[i] != prefix[i])
            {
                return false;
            }
        }
        return true;
    }
}

TEST_CASE("pipeline.registration: Pipeline-collection types carry the Pipeline domain, never Editor")
{
    // The P0 reorg moved asset/importer/cook types into the Pipeline collection, but their
    // registrations still tagged TypeDomain("Editor") - false twice over (the headless CLI/MCP
    // hosts have them WITHOUT the editor; the player has them not at all). After the sweep every
    // rtti::pipeline authoring type must be Pipeline, never Editor and never defaulted to Runtime.
    pipeline::RegisterPipelineTypes();
    const TypeRegistry& reg = GlobalTypeRegistry();
    const TypeDomain editor{StringView(u8"Editor")};

    usize pipelineDomained = 0;
    for (const TypeInfo* type : reg.All())
    {
        if (!NamespaceStartsWith(type->namespaceName, "rtti::pipeline"))
        {
            continue;
        }
        const TypeDomain domain = reg.DomainOf(type->id);
        CHECK(domain != editor); // the drift this test exists to catch
        if (domain == pipeline::kPipelineTypeDomain)
        {
            ++pipelineDomained;
        }
    }
    // The swept asset types (one-plus per pipeline collection) landed on Pipeline - proving the
    // sweep took effect and did not silently default them to Runtime.
    CHECK(pipelineDomained >= 14);
}

TEST_CASE("pipeline.registration: every builder's ProductType is a registered serializable")
{
    // Pass-17 tripwire: the cook driver stamps builder->ProductType() into the product envelope
    // and the factory reconstructs it via ReadObject - so the product type MUST be a registered
    // serializable (the cooked SERIALIZED form). Three builders (Terrain/Heightfield/SplatWeights)
    // shipped returning their RUNTIME product type at once (1bb4b31d); this audits all of them,
    // including builder N+1.
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry registry;
    pipeline::RegisterAllBuilders(registry);
    REQUIRE(registry.Count() == pipeline::kBuilderCount);

    registry.ForEach(
        [](const pipeline::IAssetBuilder& builder)
        {
            const TypeInfo* product = builder.ProductType();
            REQUIRE(product != nullptr);
            INFO("builder product type: ",
                 doctest::String(product->name != nullptr ? product->name : "<unnamed>"));
            CHECK(GlobalSerializableRegistry().Contains(product->id));
        });
}
