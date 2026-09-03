// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.rhi;
import foundation.rhi.null;
import foundation.rhi.validation;

using namespace foundation::core;
using namespace foundation::rhi;

TEST_CASE("rhi.validation: wraps a backend and forwards valid calls")
{
    Backend* inner = nullptr;
    REQUIRE(null::CreateNullBackend(inner).IsOk());

    // Heap-create through the factory: Destroy() is the wrapper's only teardown path
    // (it frees the adapter wrappers, destroys the inner backend, then itself).
    Backend* vb = validation::CreateValidatedBackend(inner, DefaultAllocator());
    REQUIRE(vb != nullptr);
    auto adapters = vb->EnumerateAdapters();
    REQUIRE(adapters.Size() >= 1u);

    Device* device = nullptr;
    REQUIRE(adapters[0]->CreateDevice(DeviceDesc{}, device).IsOk());
    REQUIRE(device != nullptr);

    BufferDesc bufferDesc{};
    bufferDesc.size = 128;
    Buffer* buffer = nullptr;
    CHECK(device->CreateBuffer(bufferDesc, buffer).IsOk());
    CHECK(buffer != nullptr);

    device->DestroyBuffer(buffer);
    device->Destroy();
    vb->Destroy();
}

TEST_CASE("rhi.validation: catches invalid usage (null texture)")
{
    Backend* inner = nullptr;
    REQUIRE(null::CreateNullBackend(inner).IsOk());

    Backend* vb = validation::CreateValidatedBackend(inner, DefaultAllocator());
    REQUIRE(vb != nullptr);
    Device* device = nullptr;
    REQUIRE(vb->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    // The validation layer rejects a null texture (and logs a diagnostic) instead
    // of forwarding it to the backend.
    TextureView* view = nullptr;
    CHECK_FALSE(device->CreateTextureView(nullptr, TextureViewDesc{}, view).IsOk());
    CHECK(view == nullptr);

    device->Destroy();
    vb->Destroy();
}
