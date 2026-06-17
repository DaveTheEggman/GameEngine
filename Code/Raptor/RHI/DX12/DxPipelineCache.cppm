/// DX12 implementation of PipelineCache via ID3D12PipelineLibrary.
/// Ported from Sedulous.RHI.DX12/DX12PipelineCache.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

export module raptor.rhi.dx12:pipeline_cache;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxPipelineCacheImpl : public PipelineCache {
public:
    Status init(ID3D12Device* device, const PipelineCacheDesc& d) {
        ComPtr<ID3D12Device1> device1;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) return ErrorCode::Unknown;

        HRESULT hr;
        if (d.initialData.Size() > 0)
            hr = device1->CreatePipelineLibrary(d.initialData.Data(), d.initialData.Size(), IID_PPV_ARGS(&library_));
        else
            hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library_));

        return SUCCEEDED(hr) ? ErrorCode::Ok : ErrorCode::Unknown;
    }

    u32 GetDataSize() override {
        if (!library_) return 0;
        return static_cast<u32>(library_->GetSerializedSize());
    }

    Status GetData(Span<u8> outData) override {
        if (!library_) return ErrorCode::Unknown;
        auto size = library_->GetSerializedSize();
        if (outData.Size() < size) return ErrorCode::Unknown;
        return SUCCEEDED(library_->Serialize(outData.Data(), size)) ? ErrorCode::Ok : ErrorCode::Unknown;
    }

    void cleanup() { library_.Reset(); }

    [[nodiscard]] ID3D12PipelineLibrary* handle() const { return library_.Get(); }

private:
    ComPtr<ID3D12PipelineLibrary> library_;
};

} // namespace raptor::rhi::dx12
