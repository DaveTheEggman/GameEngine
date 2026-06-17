/// DX12 implementation of Surface. Simply stores the HWND.
/// Ported from Sedulous.RHI.DX12/DX12Surface.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:surface;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxSurfaceImpl : public Surface {
public:
    explicit DxSurfaceImpl(HWND hwnd) : hwnd_(hwnd) {}

    [[nodiscard]] HWND handle() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

} // namespace raptor::rhi::dx12
