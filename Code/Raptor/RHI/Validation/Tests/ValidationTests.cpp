#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rhi.null;
import raptor.rhi.validation;

using namespace raptor::core;
using namespace raptor::rhi;

TEST_CASE("rhi.validation: wraps a backend and forwards valid calls")
{
    Backend* inner = nullptr;
    REQUIRE(null::CreateNullBackend(inner).IsOk());

    validation::ValidatedBackend vb(inner);
    auto adapters = vb.EnumerateAdapters();
    REQUIRE(adapters.Size() >= 1u);

    Device* device = nullptr;
    REQUIRE(adapters[0]->CreateDevice(DeviceDesc{}, device).IsOk());
    REQUIRE(device != nullptr);

    BufferDesc bufferDesc{};
    bufferDesc.size = 128;
    Buffer* buffer = nullptr;
    CHECK(device->CreateBuffer(bufferDesc, buffer).IsOk());
    CHECK(buffer != nullptr);
}

TEST_CASE("rhi.validation: catches invalid usage (null texture)")
{
    Backend* inner = nullptr;
    REQUIRE(null::CreateNullBackend(inner).IsOk());

    validation::ValidatedBackend vb(inner);
    Device* device = nullptr;
    REQUIRE(vb.EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    // The validation layer rejects a null texture (and logs a diagnostic) instead
    // of forwarding it to the backend.
    TextureView* view = nullptr;
    CHECK_FALSE(device->CreateTextureView(nullptr, TextureViewDesc{}, view).IsOk());
    CHECK(view == nullptr);
}
