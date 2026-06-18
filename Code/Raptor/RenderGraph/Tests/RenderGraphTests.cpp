#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;

using namespace raptor::core;
using namespace raptor::rendergraph;
namespace rhi = raptor::rhi;

TEST_CASE("rendergraph.types: handles and validity")
{
    CHECK_FALSE(RGHandle::Invalid().IsValid());
    CHECK_FALSE(PassHandle::Invalid().IsValid());
    CHECK(RGHandle::Invalid() == RGHandle::Invalid());
    CHECK(RGHandle{ 3, 1 } != RGHandle{ 3, 2 });   // generation matters
    CHECK(RGHandle{ 5, 0 }.IsValid());
}

TEST_CASE("rendergraph.types: access read/write classification + state mapping")
{
    CHECK(IsRead(RGAccessType::ReadTexture));
    CHECK_FALSE(IsWrite(RGAccessType::ReadTexture));
    CHECK(IsWrite(RGAccessType::WriteColorTarget));
    CHECK(IsRead(RGAccessType::ReadWriteStorage));
    CHECK(IsWrite(RGAccessType::ReadWriteStorage));

    CHECK(ToResourceState(RGAccessType::WriteColorTarget) == rhi::ResourceState::RenderTarget);
    CHECK(ToResourceState(RGAccessType::ReadTexture) == rhi::ResourceState::ShaderRead);
    CHECK(ToResourceState(RGAccessType::WriteDepthTarget) == rhi::ResourceState::DepthStencilWrite);
    CHECK(ToResourceState(RGAccessType::ReadWriteStorage)
          == (rhi::ResourceState::ShaderWrite | rhi::ResourceState::ShaderRead));
}

TEST_CASE("rendergraph.types: subresource overlap")
{
    CHECK(RGSubresourceRange::All().IsAll());

    // Different array layers don't overlap.
    RGSubresourceRange layer0{ 0, 1, 0, 1 };
    RGSubresourceRange layer1{ 0, 1, 1, 1 };
    CHECK_FALSE(layer0.Overlaps(layer1));
    CHECK(layer0.Overlaps(layer0));

    // "All" overlaps any specific subresource.
    CHECK(RGSubresourceRange::All().Overlaps(layer1, 4, 4));
}

TEST_CASE("rendergraph.descriptors: size resolve + RHI desc conversion")
{
    RGTextureDesc desc;
    desc.format = rhi::TextureFormat::RGBA8Unorm;
    desc.sizeMode = SizeMode::HalfSize;
    desc.Resolve(1280, 720);
    CHECK(desc.width == 640u);
    CHECK(desc.height == 360u);

    const rhi::TextureDesc rhiDesc = desc.ToTextureDesc(u"gbuffer");
    CHECK(rhiDesc.format == rhi::TextureFormat::RGBA8Unorm);
    CHECK(rhiDesc.width == 640u);
    CHECK(rhiDesc.height == 360u);
    CHECK(rhiDesc.mipLevelCount == 1u);

    RGTextureDesc custom;
    custom.sizeMode = SizeMode::Custom;
    custom.width = 256; custom.height = 256;
    custom.Resolve(1280, 720);     // custom is untouched
    CHECK(custom.width == 256u);
}
