#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rhi.null;

using namespace raptor::core;
using namespace raptor::rhi;

TEST_CASE("rhi.null: backend enumerates an adapter and creates a device")
{
    Backend* backend = nullptr;
    REQUIRE(null::createNullBackend(backend).IsOk());
    REQUIRE(backend != nullptr);
    CHECK(backend->isInitialized);

    auto adapters = backend->enumerateAdapters();
    REQUIRE(adapters.Size() == 1u);

    const AdapterInfo info = adapters[0]->info();
    CHECK(info.name == u"Null Device");
    CHECK(info.type == AdapterType::Cpu);

    Device* device = nullptr;
    REQUIRE(adapters[0]->createDevice(DeviceDesc{}, device).IsOk());
    REQUIRE(device != nullptr);

    backend->destroy();
}

TEST_CASE("rhi.null: device creates resources and a mappable buffer")
{
    Backend* backend = nullptr;
    REQUIRE(null::createNullBackend(backend).IsOk());
    Device* device = nullptr;
    REQUIRE(backend->enumerateAdapters()[0]->createDevice(DeviceDesc{}, device).IsOk());

    BufferDesc bufferDesc{};
    bufferDesc.size = 256;
    Buffer* buffer = nullptr;
    REQUIRE(device->createBuffer(bufferDesc, buffer).IsOk());
    REQUIRE(buffer != nullptr);
    CHECK(buffer->map() != nullptr);   // null backend backs it with host memory
    buffer->unmap();

    Texture* texture = nullptr;
    CHECK(device->createTexture(TextureDesc{}, texture).IsOk());
    CHECK(texture != nullptr);

    // Unsupported extensions degrade, they don't crash.
    MeshPipeline* mesh = nullptr;
    CHECK(device->createMeshPipeline(MeshPipelineDesc{}, mesh).Code() == ErrorCode::NotSupported);

    backend->destroy();
}
