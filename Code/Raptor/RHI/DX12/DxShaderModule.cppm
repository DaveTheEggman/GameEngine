/// DX12 implementation of ShaderModule. Stores DXIL bytecode.
/// Ported from Sedulous.RHI.DX12/DX12ShaderModule.bf.

module;
#include "Core/Prelude.h"

#include <cstring>

export module raptor.rhi.dx12:shader_module;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxShaderModuleImpl : public ShaderModule {
public:
    Status init(const ShaderModuleDesc& d) {
        bytecode_.Resize(d.code.Size());
        std::memcpy(bytecode_.Data(), d.code.Data(), d.code.Size());
        return ErrorCode::Ok;
    }

    void cleanup() { bytecode_.Clear(); }

    [[nodiscard]] Span<const u8> bytecode() const { return { bytecode_.Data(), bytecode_.Size() }; }

private:
    Array<u8> bytecode_;
};

} // namespace raptor::rhi::dx12
