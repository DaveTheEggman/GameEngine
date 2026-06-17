/// DX12 implementation of PipelineLayout.
/// Creates ID3D12RootSignature from bind group layouts + push constant ranges.
/// Ported from Sedulous.RHI.DX12/DX12PipelineLayout.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:pipeline_layout;

import raptor.core;
import raptor.rhi;
import :conversions;
import :bind_group_layout;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

struct DynamicRootEntry {
    u32 groupIndex;
    u32 dynamicIndex;
    i32 rootParamIndex;
    D3D12_ROOT_PARAMETER_TYPE paramType;
};

class DxPipelineLayoutImpl : public PipelineLayout {
public:
    Status init(ID3D12Device* device, const PipelineLayoutDesc& d) {
        numBindGroups_ = static_cast<u32>(d.bindGroupLayouts.Size());

        Array<D3D12_ROOT_PARAMETER> rootParams;
        // Storage for descriptor ranges (must outlive SerializeRootSignature).
        Array<Array<D3D12_DESCRIPTOR_RANGE>> rangeStorage;

        rootParamMap_.Resize(d.bindGroupLayouts.Size() * 2, -1);

        for (usize gi = 0; gi < d.bindGroupLayouts.Size(); ++gi) {
            auto* layout = static_cast<DxBindGroupLayoutImpl*>(d.bindGroupLayouts[gi]);
            if (!layout) return ErrorCode::Unknown;

            Array<D3D12_DESCRIPTOR_RANGE> csvRanges, sampRanges;
            u32 dynIdx = 0;

            auto ranges = layout->ranges();
            for (usize ri = 0; ri < ranges.Size(); ++ri) {
                const auto& r = ranges[ri];

                if (r.hasDynamicOffset) {
                    D3D12_ROOT_PARAMETER_TYPE pt;
                    switch (r.type) {
                    case BindingType::UniformBuffer:          pt = D3D12_ROOT_PARAMETER_TYPE_CBV; break;
                    case BindingType::StorageBufferReadOnly:  pt = D3D12_ROOT_PARAMETER_TYPE_SRV; break;
                    case BindingType::StorageBufferReadWrite: pt = D3D12_ROOT_PARAMETER_TYPE_UAV; break;
                    default: continue;
                    }
                    D3D12_ROOT_PARAMETER p{}; p.ParameterType = pt;
                    p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    p.Descriptor.ShaderRegister = r.binding;
                    p.Descriptor.RegisterSpace  = static_cast<UINT>(gi);

                    dynamicRootEntries_.PushBack({ static_cast<u32>(gi), dynIdx,
                        static_cast<i32>(rootParams.Size()), pt });
                    rootParams.PushBack(p);
                    ++dynIdx;
                    continue;
                }

                D3D12_DESCRIPTOR_RANGE dr{};
                dr.RangeType          = toDescriptorRangeType(r.type);
                dr.NumDescriptors     = r.count;
                dr.BaseShaderRegister = r.binding;
                dr.RegisterSpace      = static_cast<UINT>(gi);
                dr.OffsetInDescriptorsFromTableStart = r.heapOffset;

                if (r.isSampler) sampRanges.PushBack(dr);
                else             csvRanges.PushBack(dr);
            }

            if (!csvRanges.IsEmpty()) {
                rangeStorage.PushBack(static_cast<Array<D3D12_DESCRIPTOR_RANGE>&&>(csvRanges));
                auto& stored = rangeStorage.Back();
                D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(stored.Size());
                p.DescriptorTable.pDescriptorRanges   = stored.Data();
                rootParamMap_[gi * 2] = static_cast<i32>(rootParams.Size());
                rootParams.PushBack(p);
            }
            if (!sampRanges.IsEmpty()) {
                rangeStorage.PushBack(static_cast<Array<D3D12_DESCRIPTOR_RANGE>&&>(sampRanges));
                auto& stored = rangeStorage.Back();
                D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(stored.Size());
                p.DescriptorTable.pDescriptorRanges   = stored.Data();
                rootParamMap_[gi * 2 + 1] = static_cast<i32>(rootParams.Size());
                rootParams.PushBack(p);
            }
        }

        // Push constants → root 32-bit constants.
        for (usize i = 0; i < d.pushConstantRanges.Size(); ++i) {
            const auto& pc = d.pushConstantRanges[i];
            if (pushConstantRootIndex_ < 0)
                pushConstantRootIndex_ = static_cast<i32>(rootParams.Size());

            D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p.Constants.ShaderRegister = pc.offset / 4;
            p.Constants.RegisterSpace  = numBindGroups_;
            p.Constants.Num32BitValues = pc.size / 4;
            rootParams.PushBack(p);
        }

        // Serialize and create root signature.
        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = static_cast<UINT>(rootParams.Size());
        rsDesc.pParameters   = rootParams.Data();
        rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) LogErrorf("DxPipelineLayout: %s", static_cast<const char*>(errBlob->GetBufferPointer()));
            return ErrorCode::Unknown;
        }

        hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSig_));
        return SUCCEEDED(hr) ? ErrorCode::Ok : ErrorCode::Unknown;
    }

    void cleanup() { rootSig_.Reset(); }

    [[nodiscard]] ID3D12RootSignature* handle() const { return rootSig_.Get(); }
    [[nodiscard]] i32 getCbvSrvUavRootIndex(u32 gi) const { return (gi*2 < rootParamMap_.Size()) ? rootParamMap_[gi*2] : -1; }
    [[nodiscard]] i32 getSamplerRootIndex(u32 gi)   const { return (gi*2+1 < rootParamMap_.Size()) ? rootParamMap_[gi*2+1] : -1; }
    [[nodiscard]] i32 pushConstantRootIndex()        const { return pushConstantRootIndex_; }
    [[nodiscard]] u32 numBindGroups()                const { return numBindGroups_; }
    [[nodiscard]] Span<const DynamicRootEntry> dynamicRootEntries() const { return { dynamicRootEntries_.Data(), dynamicRootEntries_.Size() }; }

private:
    ComPtr<ID3D12RootSignature>    rootSig_;
    Array<i32>                     rootParamMap_;
    Array<DynamicRootEntry>        dynamicRootEntries_;
    i32                            pushConstantRootIndex_ = -1;
    u32                            numBindGroups_ = 0;
};

} // namespace raptor::rhi::dx12
