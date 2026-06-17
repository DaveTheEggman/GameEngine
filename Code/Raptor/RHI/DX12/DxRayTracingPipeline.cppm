/// DX12 implementation of RayTracingPipeline.
/// Wraps a D3D12 state object created via ID3D12Device5::CreateStateObject.
/// Ported from Sedulous.RHI.DX12/DX12RayTracingPipeline.bf.

module;
#include "Core/Prelude.h"

#include "DxIncludes.h"

#include <string>
#include <vector>

export module raptor.rhi.dx12:ray_tracing_pipeline;

import raptor.core;
import raptor.rhi;
import :shader_module;
import :pipeline_layout;

using namespace raptor::core;

export namespace raptor::rhi::dx12 {

class DxRayTracingPipelineImpl : public RayTracingPipeline {
public:
    Status init(ID3D12Device* device, const RayTracingPipelineDesc& desc) {
        layout_ = static_cast<DxPipelineLayoutImpl*>(desc.layout);
        if (!layout_) {
            LogErrorf("DxRayTracingPipeline: pipeline layout is null");
            return ErrorCode::Unknown;
        }
        layout = desc.layout;

        // Query ID3D12Device5 for CreateStateObject.
        ComPtr<ID3D12Device5> device5;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device5));
        if (FAILED(hr) || !device5) {
            LogErrorf("DxRayTracingPipeline: QueryInterface for ID3D12Device5 failed (0x%08X)",
                      static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        // Collect entry point names and per-stage export descriptors.
        // StringView is wide in Raptor, and DX12 expects LPCWSTR — direct copy.
        std::vector<std::wstring> entryWide;
        Array<D3D12_EXPORT_DESC> exports;

        for (usize i = 0; i < desc.stages.Size(); ++i) {
            const auto& stage = desc.stages[i];
            auto* dxMod = static_cast<DxShaderModuleImpl*>(stage.module);
            if (!dxMod) continue;

            entryWide.emplace_back(
                reinterpret_cast<const wchar_t*>(stage.entryPoint.Data()),
                stage.entryPoint.Size());
        }

        exports.Resize(entryWide.size());
        for (usize i = 0; i < entryWide.size(); ++i) {
            exports[i] = {};
            exports[i].Name  = entryWide[i].c_str();
            exports[i].Flags = D3D12_EXPORT_FLAG_NONE;
        }

        // Build one DXIL library subobject per stage.
        Array<D3D12_DXIL_LIBRARY_DESC> libraries;
        for (usize i = 0; i < desc.stages.Size(); ++i) {
            auto* dxMod = static_cast<DxShaderModuleImpl*>(desc.stages[i].module);
            if (!dxMod) continue;

            auto bc = dxMod->bytecode();
            D3D12_DXIL_LIBRARY_DESC lib{};
            lib.DXILLibrary.pShaderBytecode = bc.Data();
            lib.DXILLibrary.BytecodeLength  = bc.Size();
            lib.NumExports = 1;
            lib.pExports   = &exports[i];
            libraries.PushBack(lib);
        }

        // Count hit groups.
        u32 numHitGroups = 0;
        for (usize i = 0; i < desc.groups.Size(); ++i) {
            if (desc.groups[i].type == RayTracingShaderGroup::Type::TrianglesHitGroup ||
                desc.groups[i].type == RayTracingShaderGroup::Type::ProceduralHitGroup)
                ++numHitGroups;
        }

        // Total subobjects: libraries + hit groups + shader config + pipeline config + global root sig.
        usize subobjectCount = libraries.Size() + numHitGroups + 3;
        Array<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);
        usize soIdx = 0;

        // --- DXIL library subobjects ---
        for (usize i = 0; i < libraries.Size(); ++i) {
            subobjects[soIdx].Type  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            subobjects[soIdx].pDesc = &libraries[i];
            ++soIdx;
        }

        // --- Hit groups ---
        Array<D3D12_HIT_GROUP_DESC> hitGroups;
        std::vector<std::wstring> hitGroupNamesWide;

        for (usize i = 0; i < desc.groups.Size(); ++i) {
            const auto& group = desc.groups[i];
            if (group.type != RayTracingShaderGroup::Type::TrianglesHitGroup &&
                group.type != RayTracingShaderGroup::Type::ProceduralHitGroup)
                continue;

            // Format "HitGroupN" where N is the group index.
            std::wstring hgName = L"HitGroup" + std::to_wstring(i);
            hitGroupNamesWide.push_back(hgName);

            D3D12_HIT_GROUP_DESC hg{};
            hg.HitGroupExport = hitGroupNamesWide.back().c_str();
            hg.Type = (group.type == RayTracingShaderGroup::Type::TrianglesHitGroup)
                ? D3D12_HIT_GROUP_TYPE_TRIANGLES
                : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

            if (group.closestHitShaderIndex != ~0u && group.closestHitShaderIndex < entryWide.size())
                hg.ClosestHitShaderImport = entryWide[group.closestHitShaderIndex].c_str();

            if (group.anyHitShaderIndex != ~0u && group.anyHitShaderIndex < entryWide.size())
                hg.AnyHitShaderImport = entryWide[group.anyHitShaderIndex].c_str();

            if (group.intersectionShaderIndex != ~0u && group.intersectionShaderIndex < entryWide.size())
                hg.IntersectionShaderImport = entryWide[group.intersectionShaderIndex].c_str();

            hitGroups.PushBack(hg);
        }

        for (usize i = 0; i < hitGroups.Size(); ++i) {
            subobjects[soIdx].Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            subobjects[soIdx].pDesc = &hitGroups[i];
            ++soIdx;
        }

        // --- Build group-to-export-name mapping ---
        // For each group in desc.Groups order, store the DX12 export name
        // used to retrieve its shader identifier.
        for (usize i = 0; i < desc.groups.Size(); ++i) {
            const auto& group = desc.groups[i];
            if (group.type == RayTracingShaderGroup::Type::General) {
                // General groups (raygen/miss/callable) use the entry point name.
                if (group.generalShaderIndex != ~0u && group.generalShaderIndex < desc.stages.Size()) {
                    auto ep = desc.stages[group.generalShaderIndex].entryPoint;
                    groupExportNames_.emplace_back(
                        reinterpret_cast<const wchar_t*>(ep.Data()), ep.Size());
                } else {
                    groupExportNames_.emplace_back();
                }
            } else {
                // Hit groups use "HitGroupN" where N is the group index.
                groupExportNames_.push_back(L"HitGroup" + std::to_wstring(i));
            }
        }

        // --- Shader config ---
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes   = (desc.maxPayloadSize > 0)   ? desc.maxPayloadSize   : 32;
        shaderConfig.MaxAttributeSizeInBytes  = (desc.maxAttributeSize > 0) ? desc.maxAttributeSize : 8;
        subobjects[soIdx].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobjects[soIdx].pDesc = &shaderConfig;
        ++soIdx;

        // --- Pipeline config ---
        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
        pipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;
        subobjects[soIdx].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        subobjects[soIdx].pDesc = &pipelineConfig;
        ++soIdx;

        // --- Global root signature ---
        D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig{};
        globalRootSig.pGlobalRootSignature = layout_->handle();
        subobjects[soIdx].Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subobjects[soIdx].pDesc = &globalRootSig;
        ++soIdx;

        // --- Create state object ---
        D3D12_STATE_OBJECT_DESC stateObjDesc{};
        stateObjDesc.Type          = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjDesc.NumSubobjects = static_cast<UINT>(soIdx);
        stateObjDesc.pSubobjects   = subobjects.Data();

        hr = device5->CreateStateObject(&stateObjDesc, IID_PPV_ARGS(&stateObject_));
        if (FAILED(hr) || !stateObject_) {
            LogErrorf("DxRayTracingPipeline: CreateStateObject failed (0x%08X)",
                      static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        // Query properties for shader identifier lookup.
        hr = stateObject_->QueryInterface(IID_PPV_ARGS(&properties_));
        if (FAILED(hr)) properties_.Reset();

        return ErrorCode::Ok;
    }

    /// Gets the shader identifier for an export name.
    /// Returns a pointer to D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES (32) bytes,
    /// or nullptr on failure.
    [[nodiscard]] void* getShaderIdentifier(StringView exportName) const {
        if (!properties_) return nullptr;

        // Convert narrow string to wide (ASCII-safe for shader entry points).
        std::wstring wide;
        wide.reserve(exportName.Size());
        for (usize i = 0; i < exportName.Size(); ++i)
            wide.push_back(static_cast<wchar_t>(exportName[i]));

        return properties_->GetShaderIdentifier(wide.c_str());
    }

    void cleanup() {
        properties_.Reset();
        stateObject_.Reset();
    }

    [[nodiscard]] ID3D12StateObject*           handle()     const { return stateObject_.Get(); }
    [[nodiscard]] ID3D12StateObjectProperties* properties() const { return properties_.Get(); }
    [[nodiscard]] DxPipelineLayoutImpl*         pipelineLayout() const { return layout_; }
    [[nodiscard]] Span<const std::wstring>       groupExportNames() const {
        return { groupExportNames_.data(), groupExportNames_.size() };
    }

private:
    ComPtr<ID3D12StateObject>           stateObject_;
    ComPtr<ID3D12StateObjectProperties> properties_;
    DxPipelineLayoutImpl*               layout_ = nullptr;
    std::vector<std::wstring>           groupExportNames_;
};

} // namespace raptor::rhi::dx12
