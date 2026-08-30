// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// foundation.rhi.webgpu:surface - Surface over WGPUSurface.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module foundation.rhi.webgpu:surface;

import foundation.core;
import foundation.rhi;
import :api;

using namespace foundation::core;

export namespace foundation::rhi::webgpu
{
    class WebGpuSurface final : public Surface
    {
    public:
        void Adopt(const WebGpuApi& api, WGPUSurface surface)
        {
            m_api = &api;
            m_surface = surface;
        }

        void Release()
        {
            if (m_surface != nullptr)
            {
                m_api->wgpuSurfaceRelease(m_surface);
                m_surface = nullptr;
            }
        }

        [[nodiscard]] WGPUSurface Handle() const { return m_surface; }

    private:
        const WebGpuApi* m_api = nullptr;
        WGPUSurface m_surface = nullptr;
    };
}
