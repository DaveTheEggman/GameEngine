// ImageAsset's reflected surface (its colorSpace enum property).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import foundation.core;
import foundation.image;
import image.pipeline;

using namespace foundation::core;
using namespace pipeline;

TEST_CASE("reflection-p1: ImageAsset exposes colorSpace as a reflected enum")
{
    pipeline::RegisterImageAsset();
    const TypeInfo& type = pipeline::ImageAsset::StaticType();

    CHECK(PropertyCount(type) == 1u);
    const PropertyInfo* cs = FindProperty(type, "colorSpace");
    REQUIRE(cs != nullptr);
    REQUIRE(cs->type != nullptr);
    CHECK(IsEnum(*cs->type));

    // Round-trip through the address escape-hatch (enum Variants aren't constructible at runtime).
    pipeline::ImageAsset asset;
    Instance inst = Instance::From(&asset);
    REQUIRE(cs->address != nullptr);
    auto* field = static_cast<foundation::image::ImageColorSpace*>(cs->address(inst));
    REQUIRE(field != nullptr);
    *field = foundation::image::ImageColorSpace::Linear;
    CHECK(asset.colorSpace == foundation::image::ImageColorSpace::Linear);
}
