/// DX12 implementation of Surface. Simply stores the HWND.
/// Ported from Sedulous.RHI.DX12/DX12Surface.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module draconic.rhi.dx12:surface;

import draconic.core;
import draconic.rhi;

using namespace foundation::core;

export namespace foundation::rhi::dx12
{

    class DxSurfaceImpl : public Surface
    {
    public:
        explicit DxSurfaceImpl(HWND hwnd) : m_hwnd(hwnd) {}

        [[nodiscard]] HWND handle() const { return m_hwnd; }

    private:
        HWND m_hwnd = nullptr;
    };

} // namespace foundation::rhi::dx12
