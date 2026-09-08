// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>
#include <cstring>

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

namespace
{
    // Captures RHI error lines so a void-returning validation report can be asserted on.
    struct ErrorCapture
    {
        int errors = 0;
        int matching = 0;
        const char* needle = "";
        static bool Sink(void* context, bool error, const char* utf8)
        {
            auto* self = static_cast<ErrorCapture*>(context);
            if (error)
            {
                ++self->errors;
                if (std::strstr(utf8, self->needle) != nullptr)
                    ++self->matching;
            }
            return true; // swallow: the test decides what to print
        }
    };
}

TEST_CASE("rhi.validation: a fenced submit without a fence is reported on BOTH fenced overloads")
{
    // The contract: the fenced overloads require a signal fence; the plain overload is the
    // unsignalled path. This was reported on the 3-arg overload only - the Beef port's layer
    // reported both, and its Vulkan backend rejected the submit while ours submitted
    // unsignalled (and, on a graphics queue, consumed the swap-chain sync doing so).
    Backend* inner = nullptr;
    REQUIRE(null::CreateNullBackend(inner).IsOk());
    Backend* vb = validation::CreateValidatedBackend(inner, DefaultAllocator());
    REQUIRE(vb != nullptr);
    Device* device = nullptr;
    REQUIRE(vb->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());
    Queue* queue = device->GetQueue(QueueType::Graphics, 0);
    REQUIRE(queue != nullptr);
    CommandPool* pool = nullptr;
    REQUIRE(device->CreateCommandPool(QueueType::Graphics, pool).IsOk());
    CommandEncoder* enc = nullptr;
    REQUIRE(pool->CreateEncoder(enc).IsOk());
    CommandBuffer* cb = enc->Finish();
    REQUIRE(cb != nullptr);
    CommandBuffer* cbs[1] = {cb};

    ErrorCapture capture;
    capture.needle = "signalFence is null";
    SetLogSink(&ErrorCapture::Sink, &capture);

    queue->Submit(Span<CommandBuffer* const>(cbs, 1), nullptr, 1);
    CHECK(capture.matching == 1);
    queue->Submit(Span<CommandBuffer* const>(cbs, 1), Span<Fence* const>{}, Span<const u64>{},
                  nullptr, 1);
    CHECK(capture.matching == 2);
    // The plain overload is the unsignalled path: no fence, no report.
    queue->Submit(Span<CommandBuffer* const>(cbs, 1));
    CHECK(capture.matching == 2);

    SetLogSink(nullptr, nullptr);
    pool->DestroyEncoder(enc);
    device->DestroyCommandPool(pool);
    device->Destroy();
    vb->Destroy();
}

