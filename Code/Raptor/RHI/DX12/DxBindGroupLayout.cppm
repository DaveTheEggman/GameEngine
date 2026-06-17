/// DX12 implementation of BindGroupLayout.
/// Tracks descriptor range definitions and heap offset info.
/// Ported from Sedulous.RHI.DX12/DX12BindGroupLayout.bf.

module;
#include "Core/Prelude.h"

#include <cstdint>

export module raptor.rhi.dx12:bind_group_layout;

import raptor.core;
import raptor.rhi;
import :conversions;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

struct DxBindingRangeInfo {
    u32         binding = 0;
    BindingType type    = BindingType::UniformBuffer;
    u32         count   = 0;
    u32         heapOffset = 0;
    bool        isSampler  = false;
    bool        hasDynamicOffset = false;
    u32         storageBufferStride = 0;
};

class DxBindGroupLayoutImpl : public BindGroupLayout {
public:
    Status init(const BindGroupLayoutDesc& d) {
        u32 csvUavOff = 0, sampOff = 0;

        for (usize i = 0; i < d.entries.Size(); ++i) {
            const auto& e = d.entries[i];
            entries_.PushBack(e);

            bool sampler = isSamplerBinding(e.type);
            u32 cnt = e.count;
            if (cnt == ~0u) { cnt = 1024 * 16; hasBindless_ = true; }

            DxBindingRangeInfo r{};
            r.binding    = e.binding;
            r.type       = e.type;
            r.count      = cnt;
            r.isSampler  = sampler;
            r.hasDynamicOffset = e.hasDynamicOffset;
            r.storageBufferStride = e.storageBufferStride;

            if (e.hasDynamicOffset) {
                r.heapOffset = 0;
                ++dynamicOffsetCount_;
            } else if (sampler) {
                r.heapOffset = sampOff;
                sampOff += cnt;
            } else {
                r.heapOffset = csvUavOff;
                csvUavOff += cnt;
            }
            ranges_.PushBack(r);
        }
        cbvSrvUavCount_ = csvUavOff;
        samplerCount_   = sampOff;
        return ErrorCode::Ok;
    }

    [[nodiscard]] Span<const BindGroupLayoutEntry> entries() const { return { entries_.Data(), entries_.Size() }; }
    [[nodiscard]] Span<const DxBindingRangeInfo>   ranges()  const { return { ranges_.Data(), ranges_.Size() }; }
    [[nodiscard]] u32  cbvSrvUavCount()    const { return cbvSrvUavCount_; }
    [[nodiscard]] u32  samplerCount()      const { return samplerCount_; }
    [[nodiscard]] u32  dynamicOffsetCount()const { return dynamicOffsetCount_; }
    [[nodiscard]] bool hasBindless()       const { return hasBindless_; }

private:
    Array<BindGroupLayoutEntry> entries_;
    Array<DxBindingRangeInfo>   ranges_;
    u32  cbvSrvUavCount_    = 0;
    u32  samplerCount_      = 0;
    u32  dynamicOffsetCount_= 0;
    bool hasBindless_       = false;
};

} // namespace raptor::rhi::dx12
