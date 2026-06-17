/// Vulkan implementation of BindGroup.
/// Ported from Sedulous.RHI.Vulkan/VulkanBindGroup.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:bind_group;

import raptor.core;
import raptor.rhi;
import :conversions;
import :binding_shifts;
import :bind_group_layout;
import :buffer;
import :texture;
import :texture_view;
import :sampler;
import :accel_struct;
import :descriptor_pool_manager;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkBindGroupImpl : public BindGroup {
public:
    Status init(VkDevice device, VkDescriptorPoolManager* poolMgr, const BindGroupDesc& desc,
                 const BindingShifts& shifts = {}) {
        device_ = device;
        shifts_ = shifts;
        layout_ = static_cast<VkBindGroupLayoutImpl*>(desc.layout);
        if (!layout_) return ErrorCode::Unknown;

        VkDescriptorPool pool;
        if (poolMgr->allocate(layout_->handle(), pool, layout_->hasBindless(), layout_->bindlessCount()) != ErrorCode::Ok)
            return ErrorCode::Unknown;
        pool_ = pool;
        set_  = poolMgr->lastAllocatedSet();

        writeDescriptors(device, desc);
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device, VkDescriptorPoolManager* poolMgr) {
        if (set_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
            poolMgr->free(pool_, set_);
            set_  = VK_NULL_HANDLE;
            pool_ = VK_NULL_HANDLE;
        }
        (void)device;
    }

    BindGroupLayout* Layout() override { return layout_; }

    void UpdateBindless(Span<const BindlessUpdateEntry> entries) override {
        if (entries.Size() == 0) return;

        Array<VkWriteDescriptorSet>    writes;
        Array<VkDescriptorBufferInfo>  bufInfos;
        Array<VkDescriptorImageInfo>   imgInfos;
        bufInfos.Reserve(entries.Size());
        imgInfos.Reserve(entries.Size());

        auto layoutEntries = layout_->Entries();

        for (usize i = 0; i < entries.Size(); ++i) {
            const auto& e = entries[i];
            if (e.layoutIndex >= static_cast<u32>(layoutEntries.Size())) continue;
            const auto& le = layoutEntries[e.layoutIndex];

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = set_;
            w.dstBinding      = shifts_.apply(le.type, le.binding);
            w.dstArrayElement = e.arrayIndex;
            w.descriptorCount = 1;
            w.descriptorType  = toVkDescriptorType(le);

            switch (le.type) {
            case BindingType::BindlessStorageBuffers:
                if (auto* vkBuf = static_cast<VkBufferImpl*>(e.buffer)) {
                    VkDescriptorBufferInfo bi{}; bi.buffer = vkBuf->handle(); bi.offset = e.bufferOffset;
                    bi.range = e.bufferSize > 0 ? e.bufferSize : VK_WHOLE_SIZE;
                    bufInfos.PushBack(bi); w.pBufferInfo = &bufInfos.Back();
                } else continue;
                break;
            case BindingType::BindlessTextures: case BindingType::BindlessStorageTextures:
                if (auto* vkView = static_cast<VkTextureViewImpl*>(e.textureView)) {
                    VkDescriptorImageInfo ii{}; ii.imageView = vkView->handle();
                    ii.imageLayout = le.type == BindingType::BindlessTextures
                        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                    imgInfos.PushBack(ii); w.pImageInfo = &imgInfos.Back();
                } else continue;
                break;
            case BindingType::BindlessSamplers:
                if (auto* vkSamp = static_cast<VkSamplerImpl*>(e.sampler)) {
                    VkDescriptorImageInfo ii{}; ii.sampler = vkSamp->handle();
                    imgInfos.PushBack(ii); w.pImageInfo = &imgInfos.Back();
                } else continue;
                break;
            default: continue;
            }
            writes.PushBack(w);
        }

        if (!writes.IsEmpty())
            vkUpdateDescriptorSets(device_, static_cast<u32>(writes.Size()), writes.Data(), 0, nullptr);
    }

    [[nodiscard]] VkDescriptorSet handle() const { return set_; }

private:
    void writeDescriptors(VkDevice device, const BindGroupDesc& desc) {
        if (desc.entries.Size() == 0) return;

        auto layoutEntries = layout_->Entries();

        Array<VkWriteDescriptorSet>    writes;
        Array<VkDescriptorBufferInfo>  bufInfos;
        Array<VkDescriptorImageInfo>   imgInfos;
        Array<VkWriteDescriptorSetAccelerationStructureKHR> asWriteInfos;
        Array<VkAccelerationStructureKHR>                   asHandles;
        bufInfos.Reserve(desc.entries.Size());
        imgInfos.Reserve(desc.entries.Size());
        asWriteInfos.Reserve(desc.entries.Size());
        asHandles.Reserve(desc.entries.Size());

        usize entryIdx = 0;
        for (usize i = 0; i < layoutEntries.Size(); ++i) {
            const auto& le = layoutEntries[i];
            // Skip bindless entries.
            switch (le.type) {
            case BindingType::BindlessTextures: case BindingType::BindlessSamplers:
            case BindingType::BindlessStorageBuffers: case BindingType::BindlessStorageTextures:
                continue;
            default: break;
            }
            if (entryIdx >= desc.entries.Size()) break;
            const auto& e = desc.entries[entryIdx++];

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = set_;
            w.dstBinding      = shifts_.apply(le.type, le.binding);
            w.dstArrayElement = 0;
            w.descriptorCount = 1;
            w.descriptorType  = toVkDescriptorType(le);

            switch (le.type) {
            case BindingType::UniformBuffer: case BindingType::StorageBufferReadOnly: case BindingType::StorageBufferReadWrite:
                if (auto* vkBuf = static_cast<VkBufferImpl*>(e.buffer)) {
                    VkDescriptorBufferInfo bi{}; bi.buffer = vkBuf->handle(); bi.offset = e.bufferOffset;
                    bi.range = e.bufferSize > 0 ? e.bufferSize : VK_WHOLE_SIZE;
                    bufInfos.PushBack(bi); w.pBufferInfo = &bufInfos.Back();
                } else continue;
                break;
            case BindingType::SampledTexture: case BindingType::StorageTextureReadOnly: case BindingType::StorageTextureReadWrite:
                if (auto* vkView = static_cast<VkTextureViewImpl*>(e.textureView)) {
                    VkDescriptorImageInfo ii{}; ii.imageView = vkView->handle();
                    if (le.type == BindingType::SampledTexture) {
                        // Depth/stencil textures are always sampled in DEPTH_STENCIL_READ_ONLY layout.
                        auto* vkTex = vkView->texture;
                        if (vkTex && IsDepthFormat(vkTex->desc.format))
                            ii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                        else
                            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    } else {
                        ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    }
                    imgInfos.PushBack(ii); w.pImageInfo = &imgInfos.Back();
                } else continue;
                break;
            case BindingType::Sampler: case BindingType::ComparisonSampler:
                if (auto* vkSamp = static_cast<VkSamplerImpl*>(e.sampler)) {
                    VkDescriptorImageInfo ii{}; ii.sampler = vkSamp->handle();
                    imgInfos.PushBack(ii); w.pImageInfo = &imgInfos.Back();
                } else continue;
                break;
            case BindingType::AccelerationStructure:
                if (auto* vkAs = static_cast<VkAccelStructImpl*>(e.accelStruct)) {
                    asHandles.PushBack(vkAs->handle());
                    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
                    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                    asInfo.accelerationStructureCount = 1;
                    asInfo.pAccelerationStructures = &asHandles.Back();
                    asWriteInfos.PushBack(asInfo);
                    w.pNext = &asWriteInfos.Back();
                } else continue;
                break;
            default: continue;
            }
            writes.PushBack(w);
        }

        if (!writes.IsEmpty())
            vkUpdateDescriptorSets(device, static_cast<u32>(writes.Size()), writes.Data(), 0, nullptr);
    }

    VkDevice              device_ = VK_NULL_HANDLE;
    VkDescriptorSet       set_    = VK_NULL_HANDLE;
    VkDescriptorPool      pool_   = VK_NULL_HANDLE;
    VkBindGroupLayoutImpl* layout_ = nullptr;
    BindingShifts          shifts_{};
};

} // namespace raptor::rhi::vk
