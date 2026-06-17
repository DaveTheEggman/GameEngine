/// DX12 implementation of ShaderModule. Stores DXIL bytecode.
/// Ported from Sedulous.RHI.DX12/DX12ShaderModule.bf.

module;

#include <cstring>
#include <vector>

export module raptor.rhi.dx12:shader_module;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::dx12 {

class DxShaderModuleImpl : public ShaderModule {
public:
    Status init(const ShaderModuleDesc& d) {
        bytecode_.resize(d.code.count());
        std::memcpy(bytecode_.data(), d.code.data(), d.code.count());
        return ErrorCode::Ok;
    }

    void cleanup() { bytecode_.clear(); }

    [[nodiscard]] Span<const u8> bytecode() const { return { bytecode_.data(), bytecode_.size() }; }

private:
    std::vector<u8> bytecode_;
};

} // namespace raptor::rhi::dx12
