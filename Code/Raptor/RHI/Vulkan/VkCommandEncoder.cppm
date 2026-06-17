/// Vulkan implementation of CommandEncoder + RayTracingEncoderExt.
/// Ported from Sedulous.RHI.Vulkan/VulkanCommandEncoder.bf.

module;

#include "VkIncludes.h"

#include <algorithm>
#include <cstring>
#include <vector>

export module raptor.rhi.vk:command_encoder;

import raptor.core;
import raptor.rhi;
import :conversions;
import :barrier_helper;
import :buffer;
import :texture;
import :texture_view;
import :bind_group;
import :pipeline_layout;
import :query_set;
import :command_buffer;
import :command_pool;
import :render_pass_encoder;
import :compute_pass_encoder;
import :accel_struct;
import :ray_tracing_pipeline;

export namespace raptor::rhi::vk {

class VkCommandEncoderImpl : public CommandEncoder, public RayTracingEncoderExt {
public:
    VkCommandEncoderImpl(VkCommandBuffer cmdBuf, VkDevice device, VkCommandPoolImpl* pool)
        : cmdBuf_(cmdBuf), device_(device), pool_(pool),
          rpe_(cmdBuf, device), cpe_(cmdBuf) {}

    // ---- CommandEncoder ----

    RenderPassEncoder* beginRenderPass(const RenderPassDesc& desc) override {
        auto colorAtts = desc.colorAttachments.view();
        std::vector<VkRenderingAttachmentInfo> vkColor(colorAtts.count());

        for (usize i = 0; i < colorAtts.count(); ++i) {
            const auto& att = colorAtts[i];
            auto* view = static_cast<VkTextureViewImpl*>(att.view);
            VkRenderingAttachmentInfo info{};
            info.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageView   = view ? view->handle() : VK_NULL_HANDLE;
            info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // Update texture layout tracking — dynamic rendering implicitly
            // transitions attachments to the specified layout.
            if (view) {
                if (auto* vkTex = static_cast<VkTextureImpl*>(view->texture))
                    vkTex->setSubresourceLayout(view->desc.baseMipLevel, 1, view->desc.baseArrayLayer, 1,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
            info.loadOp      = toVkLoadOp(att.loadOp);
            info.storeOp     = toVkStoreOp(att.storeOp);
            info.clearValue.color.float32[0] = att.clearValue.r;
            info.clearValue.color.float32[1] = att.clearValue.g;
            info.clearValue.color.float32[2] = att.clearValue.b;
            info.clearValue.color.float32[3] = att.clearValue.a;
            if (auto* rv = static_cast<VkTextureViewImpl*>(att.resolveTarget)) {
                info.resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
                info.resolveImageView   = rv->handle();
                info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            vkColor[i] = info;
        }

        VkRect2D renderArea{};
        if (colorAtts.count() > 0) {
            if (auto* v = static_cast<VkTextureViewImpl*>(colorAtts[0].view))
                renderArea.extent = { v->width(), v->height() };
        } else if (desc.depthStencilAttachment.has_value()) {
            if (auto* v = static_cast<VkTextureViewImpl*>(desc.depthStencilAttachment->view))
                renderArea.extent = { v->width(), v->height() };
        }

        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea           = renderArea;
        ri.layerCount           = 1;
        ri.colorAttachmentCount = static_cast<u32>(vkColor.size());
        ri.pColorAttachments    = vkColor.data();

        VkRenderingAttachmentInfo depthAtt{}, stencilAtt{};
        if (desc.depthStencilAttachment.has_value()) {
            const auto& ds = *desc.depthStencilAttachment;
            if (auto* dv = static_cast<VkTextureViewImpl*>(ds.view)) {
                depthAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAtt.imageView   = dv->handle();
                depthAtt.imageLayout = ds.depthReadOnly
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                // Update texture layout tracking.
                if (auto* vkTex = static_cast<VkTextureImpl*>(dv->texture))
                    vkTex->setSubresourceLayout(dv->desc.baseMipLevel, 1, dv->desc.baseArrayLayer, 1, depthAtt.imageLayout);
                depthAtt.loadOp  = toVkLoadOp(ds.depthLoadOp);
                depthAtt.storeOp = toVkStoreOp(ds.depthStoreOp);
                depthAtt.clearValue.depthStencil = { ds.depthClearValue, ds.stencilClearValue };
                ri.pDepthAttachment = &depthAtt;
                if (hasStencil(dv->format())) {
                    stencilAtt          = depthAtt;
                    stencilAtt.loadOp   = toVkLoadOp(ds.stencilLoadOp);
                    stencilAtt.storeOp  = toVkStoreOp(ds.stencilStoreOp);
                    ri.pStencilAttachment = &stencilAtt;
                }
            }
        }

        vkCmdBeginRendering(cmdBuf_, &ri);
        return &rpe_;
    }

    ComputePassEncoder* beginComputePass(StringView) override { return &cpe_; }

    void barrier(const BarrierGroup& group) override {
        std::vector<VkMemoryBarrier2>      memBs(group.memoryBarriers.count());
        std::vector<VkBufferMemoryBarrier2> bufBs(group.bufferBarriers.count());
        std::vector<VkImageMemoryBarrier2>  imgBs(group.textureBarriers.count());

        for (usize i = 0; i < group.memoryBarriers.count(); ++i) {
            auto src = getStageAccess(group.memoryBarriers[i].oldState);
            auto dst = getStageAccess(group.memoryBarriers[i].newState);
            memBs[i] = {}; memBs[i].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memBs[i].srcStageMask = src.stageMask; memBs[i].srcAccessMask = src.accessMask;
            memBs[i].dstStageMask = dst.stageMask; memBs[i].dstAccessMask = dst.accessMask;
        }

        for (usize i = 0; i < group.bufferBarriers.count(); ++i) {
            const auto& bb = group.bufferBarriers[i];
            auto src = getStageAccess(bb.oldState); auto dst = getStageAccess(bb.newState);
            bufBs[i] = {}; bufBs[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            bufBs[i].srcStageMask = src.stageMask; bufBs[i].srcAccessMask = src.accessMask;
            bufBs[i].dstStageMask = dst.stageMask; bufBs[i].dstAccessMask = dst.accessMask;
            bufBs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            if (auto* vb = static_cast<VkBufferImpl*>(bb.buffer)) bufBs[i].buffer = vb->handle();
            bufBs[i].offset = bb.offset;
            bufBs[i].size = (bb.size == ~0ull) ? VK_WHOLE_SIZE : bb.size;
        }

        for (usize i = 0; i < group.textureBarriers.count(); ++i) {
            const auto& tb = group.textureBarriers[i];
            auto src = getStageAccess(tb.oldState); auto dst = getStageAccess(tb.newState);

            auto* vkTex = static_cast<VkTextureImpl*>(tb.texture);
            TextureFormat format = vkTex ? vkTex->desc.format : TextureFormat::Undefined;

            auto newLayout = getImageLayout(tb.newState, format);

            // Resolve old layout from per-subresource tracking.
            // Tracking is kept in sync via beginRenderPass layout updates.
            VkImageLayout oldLayout;
            if (vkTex) {
                bool isWholeResource = (tb.mipLevelCount == ~0u) && (tb.arrayLayerCount == ~0u);
                if (isWholeResource)
                    oldLayout = vkTex->currentLayout;
                else
                    oldLayout = vkTex->getSubresourceLayout(tb.baseMipLevel, tb.baseArrayLayer);
                // Update tracking.
                vkTex->setSubresourceLayout(tb.baseMipLevel, tb.mipLevelCount,
                                            tb.baseArrayLayer, tb.arrayLayerCount, newLayout);
            } else {
                oldLayout = getImageLayout(tb.oldState, format);
            }

            imgBs[i] = {}; imgBs[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                imgBs[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT; imgBs[i].srcAccessMask = 0;
            } else {
                imgBs[i].srcStageMask = src.stageMask; imgBs[i].srcAccessMask = src.accessMask;
            }
            imgBs[i].dstStageMask = dst.stageMask; imgBs[i].dstAccessMask = dst.accessMask;
            imgBs[i].oldLayout = oldLayout; imgBs[i].newLayout = newLayout;
            imgBs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            if (vkTex) imgBs[i].image = vkTex->handle();
            TextureFormat fmt = vkTex ? vkTex->desc.format : TextureFormat::Undefined;
            imgBs[i].subresourceRange.aspectMask     = getAspectMask(fmt);
            imgBs[i].subresourceRange.baseMipLevel   = tb.baseMipLevel;
            imgBs[i].subresourceRange.levelCount     = tb.mipLevelCount  == ~0u ? VK_REMAINING_MIP_LEVELS   : tb.mipLevelCount;
            imgBs[i].subresourceRange.baseArrayLayer = tb.baseArrayLayer;
            imgBs[i].subresourceRange.layerCount     = tb.arrayLayerCount== ~0u ? VK_REMAINING_ARRAY_LAYERS : tb.arrayLayerCount;
        }

        VkDependencyInfo di{}; di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.memoryBarrierCount       = static_cast<u32>(memBs.size()); di.pMemoryBarriers       = memBs.data();
        di.bufferMemoryBarrierCount = static_cast<u32>(bufBs.size()); di.pBufferMemoryBarriers = bufBs.data();
        di.imageMemoryBarrierCount  = static_cast<u32>(imgBs.size()); di.pImageMemoryBarriers  = imgBs.data();
        vkCmdPipelineBarrier2(cmdBuf_, &di);
    }

    void copyBufferToBuffer(Buffer* src, u64 srcOff, Buffer* dst, u64 dstOff, u64 size) override {
        auto* s = static_cast<VkBufferImpl*>(src); auto* d = static_cast<VkBufferImpl*>(dst);
        if (!s || !d) return;
        VkBufferCopy r{}; r.srcOffset = srcOff; r.dstOffset = dstOff; r.size = size;
        vkCmdCopyBuffer(cmdBuf_, s->handle(), d->handle(), 1, &r);
    }

    void copyBufferToTexture(Buffer* src, Texture* dst, const BufferTextureCopyRegion& region) override {
        auto* s = static_cast<VkBufferImpl*>(src); auto* d = static_cast<VkTextureImpl*>(dst);
        if (!s || !d) return;
        VkBufferImageCopy c{};
        c.bufferOffset      = region.bufferOffset;
        u32 bpp2 = bytesPerPixel(d->desc.format);
        c.bufferRowLength   = (bpp2 > 0 && region.bytesPerRow > 0) ? region.bytesPerRow / bpp2 : 0;
        c.bufferImageHeight = region.rowsPerImage;
        c.imageSubresource.aspectMask     = getAspectMask(d->desc.format);
        c.imageSubresource.mipLevel       = region.textureMipLevel;
        c.imageSubresource.baseArrayLayer = region.textureArrayLayer;
        c.imageSubresource.layerCount     = 1;
        c.imageOffset = { static_cast<i32>(region.textureOrigin.x), static_cast<i32>(region.textureOrigin.y), static_cast<i32>(region.textureOrigin.z) };
        c.imageExtent = { region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth };
        vkCmdCopyBufferToImage(cmdBuf_, s->handle(), d->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }

    void copyTextureToBuffer(Texture* src, Buffer* dst, const BufferTextureCopyRegion& region) override {
        auto* s = static_cast<VkTextureImpl*>(src); auto* d = static_cast<VkBufferImpl*>(dst);
        if (!s || !d) return;
        VkBufferImageCopy c{};
        c.bufferOffset      = region.bufferOffset;
        u32 bpp = bytesPerPixel(s->desc.format);
        c.bufferRowLength   = (bpp > 0 && region.bytesPerRow > 0) ? region.bytesPerRow / bpp : 0;
        c.bufferImageHeight = region.rowsPerImage;
        c.imageSubresource.aspectMask     = getAspectMask(s->desc.format);
        c.imageSubresource.mipLevel       = region.textureMipLevel;
        c.imageSubresource.baseArrayLayer = region.textureArrayLayer;
        c.imageSubresource.layerCount     = 1;
        c.imageOffset = { static_cast<i32>(region.textureOrigin.x), static_cast<i32>(region.textureOrigin.y), static_cast<i32>(region.textureOrigin.z) };
        c.imageExtent = { region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth };
        vkCmdCopyImageToBuffer(cmdBuf_, s->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->handle(), 1, &c);
    }

    void copyTextureToTexture(Texture* src, Texture* dst, const TextureCopyRegion& region) override {
        auto* s = static_cast<VkTextureImpl*>(src); auto* d = static_cast<VkTextureImpl*>(dst);
        if (!s || !d) return;
        VkImageCopy c{};
        c.srcSubresource = { getAspectMask(s->desc.format), region.srcMipLevel, region.srcArrayLayer, 1 };
        c.dstSubresource = { getAspectMask(d->desc.format), region.dstMipLevel, region.dstArrayLayer, 1 };
        c.extent = { region.extent.width, region.extent.height, region.extent.depth };
        vkCmdCopyImage(cmdBuf_, s->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       d->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }

    void blit(Texture* src, Texture* dst) override {
        auto* s = static_cast<VkTextureImpl*>(src); auto* d = static_cast<VkTextureImpl*>(dst);
        if (!s || !d) return;
        VkImageBlit b{};
        b.srcSubresource = { getAspectMask(s->desc.format), 0, 0, 1 };
        b.srcOffsets[1]  = { static_cast<i32>(s->desc.width), static_cast<i32>(s->desc.height), 1 };
        b.dstSubresource = { getAspectMask(d->desc.format), 0, 0, 1 };
        b.dstOffsets[1]  = { static_cast<i32>(d->desc.width), static_cast<i32>(d->desc.height), 1 };
        vkCmdBlitImage(cmdBuf_, s->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       d->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
    }

    void generateMipmaps(Texture* texture) override {
        auto* vkTex = static_cast<VkTextureImpl*>(texture);
        if (!vkTex || vkTex->desc.mipLevelCount <= 1) return;
        // Simplified: caller transitions all mips to TRANSFER_DST before calling.
        i32 mipW = static_cast<i32>(vkTex->desc.width);
        i32 mipH = static_cast<i32>(vkTex->desc.height);
        auto aspect = getAspectMask(vkTex->desc.format);
        u32 layers = vkTex->desc.arrayLayerCount;

        for (u32 i = 1; i < vkTex->desc.mipLevelCount; ++i) {
            // Transition mip i-1 to TRANSFER_SRC for blit source.
            // For mip 0 (i==1), use UNDEFINED as old layout because the caller's
            // actual layout is unknown (TransferBatch leaves SHADER_READ_ONLY, etc.).
            VkImageMemoryBarrier2 srcBarrier{}; srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            srcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            srcBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            srcBarrier.oldLayout = (i == 1) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcBarrier.image = vkTex->handle();
            srcBarrier.subresourceRange = { aspect, i - 1, 1, 0, layers };

            // Transition mip i to TRANSFER_DST for blit destination.
            VkImageMemoryBarrier2 dstBarrier{}; dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            dstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            dstBarrier.srcAccessMask = 0;
            dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.image = vkTex->handle();
            dstBarrier.subresourceRange = { aspect, i, 1, 0, layers };

            VkImageMemoryBarrier2 barriers[2] = { srcBarrier, dstBarrier };
            VkDependencyInfo dep{}; dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 2; dep.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cmdBuf_, &dep);

            i32 nw = std::max(1, mipW / 2), nh = std::max(1, mipH / 2);
            VkImageBlit bl{};
            bl.srcSubresource = { aspect, i - 1, 0, layers }; bl.srcOffsets[1] = { mipW, mipH, 1 };
            bl.dstSubresource = { aspect, i,     0, layers }; bl.dstOffsets[1] = { nw, nh, 1 };
            vkCmdBlitImage(cmdBuf_, vkTex->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vkTex->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            mipW = nw; mipH = nh;
        }
        // Transition last mip to SRC.
        VkImageMemoryBarrier2 lb{}; lb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        lb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT; lb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        lb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT; lb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        lb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; lb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        lb.image = vkTex->handle();
        lb.subresourceRange = { aspect, vkTex->desc.mipLevelCount - 1, 1, 0, layers };
        VkDependencyInfo ldep{}; ldep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        ldep.imageMemoryBarrierCount = 1; ldep.pImageMemoryBarriers = &lb;
        vkCmdPipelineBarrier2(cmdBuf_, &ldep);

        // Update tracked layout — all mips now in TRANSFER_SRC.
        vkTex->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    void resolveTexture(Texture* src, Texture* dst) override {
        auto* s = static_cast<VkTextureImpl*>(src); auto* d = static_cast<VkTextureImpl*>(dst);
        if (!s || !d) return;
        auto aspect = getAspectMask(s->desc.format);
        VkImageResolve r{}; r.srcSubresource = { aspect, 0, 0, 1 }; r.dstSubresource = { aspect, 0, 0, 1 };
        r.extent = { s->desc.width, s->desc.height, 1 };
        vkCmdResolveImage(cmdBuf_, s->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          d->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    }

    void resetQuerySet(QuerySet* qs, u32 first, u32 count) override {
        if (auto* q = static_cast<VkQuerySetImpl*>(qs)) vkCmdResetQueryPool(cmdBuf_, q->handle(), first, count);
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        if (auto* q = static_cast<VkQuerySetImpl*>(qs))
            vkCmdWriteTimestamp(cmdBuf_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q->handle(), index);
    }

    void resolveQuerySet(QuerySet* qs, u32 first, u32 count, Buffer* dst, u64 dstOffset) override {
        auto* q = static_cast<VkQuerySetImpl*>(qs); auto* b = static_cast<VkBufferImpl*>(dst);
        if (q && b) vkCmdCopyQueryPoolResults(cmdBuf_, q->handle(), first, count, b->handle(), dstOffset, 8,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }

    void beginDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        char buf[256]{}; auto len = std::min(label.length(), static_cast<usize>(255));
        std::memcpy(buf, label.data(), len);
        VkDebugUtilsLabelEXT li{}; li.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        li.pLabelName = buf; li.color[0] = r; li.color[1] = g; li.color[2] = b; li.color[3] = a;
        auto pfn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(device_, "vkCmdBeginDebugUtilsLabelEXT"));
        if (pfn) pfn(cmdBuf_, &li);
    }

    void endDebugLabel() override {
        auto pfn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(device_, "vkCmdEndDebugUtilsLabelEXT"));
        if (pfn) pfn(cmdBuf_);
    }

    void insertDebugLabel(StringView label, f32 r, f32 g, f32 b, f32 a) override {
        char buf[256]{}; auto len = std::min(label.length(), static_cast<usize>(255));
        std::memcpy(buf, label.data(), len);
        VkDebugUtilsLabelEXT li{}; li.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        li.pLabelName = buf; li.color[0] = r; li.color[1] = g; li.color[2] = b; li.color[3] = a;
        auto pfn = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetDeviceProcAddr(device_, "vkCmdInsertDebugUtilsLabelEXT"));
        if (pfn) pfn(cmdBuf_, &li);
    }

    CommandBuffer* finish() override {
        vkEndCommandBuffer(cmdBuf_);
        auto* cb = new VkCommandBufferImpl(cmdBuf_);
        pool_->trackCommandBuffer(cb);
        return cb;
    }

    // ---- RayTracingEncoderExt ----

    void buildBottomLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOffset,
        Span<const AccelStructGeometryTriangles> tris, Span<const AccelStructGeometryAABBs> aabbs) override;
    void buildTopLevelAccelStruct(AccelStruct* dst, Buffer* scratch, u64 scratchOffset,
        Buffer* instanceBuf, u64 instanceOffset, u32 instanceCount) override;
    void setRayTracingPipeline(RayTracingPipeline* pipeline) override;
    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override;
    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override;
    void traceRays(Buffer* raygenSBT, u64 raygenOff, u64 raygenStride,
                   Buffer* missSBT, u64 missOff, u64 missStride,
                   Buffer* hitSBT, u64 hitOff, u64 hitStride,
                   u32 width, u32 height, u32 depth) override;

private:
    u64 getBufferDeviceAddress(VkBufferImpl* buf) {
        VkBufferDeviceAddressInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = buf->handle();
        if (!pfnGetBufAddr_) pfnGetBufAddr_ = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(
            vkGetDeviceProcAddr(device_, "vkGetBufferDeviceAddress"));
        return pfnGetBufAddr_ ? pfnGetBufAddr_(device_, &info) : 0;
    }

    VkCommandBuffer          cmdBuf_ = VK_NULL_HANDLE;
    VkDevice                 device_ = VK_NULL_HANDLE;
    VkCommandPoolImpl*       pool_   = nullptr;
    VkRenderPassEncoderImpl  rpe_;
    VkComputePassEncoderImpl cpe_;

    // RT state.
    VkRayTracingPipelineImpl* currentRtPipeline_ = nullptr;
    PFN_vkGetBufferDeviceAddress pfnGetBufAddr_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR pfnBuild_ = nullptr;
    PFN_vkCmdTraceRaysKHR pfnTrace_ = nullptr;
};

// ---- Deferred RT method implementations ----

inline void VkCommandEncoderImpl::setRayTracingPipeline(RayTracingPipeline* pipeline) {
    currentRtPipeline_ = static_cast<VkRayTracingPipelineImpl*>(pipeline);
    if (currentRtPipeline_)
        vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, currentRtPipeline_->handle());
}

inline void VkCommandEncoderImpl::setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) {
    auto* bg = static_cast<VkBindGroupImpl*>(group);
    if (!bg || !currentRtPipeline_ || !currentRtPipeline_->vkLayout()) return;
    VkDescriptorSet set = bg->handle();
    vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        currentRtPipeline_->vkLayout()->handle(), index, 1, &set,
        static_cast<u32>(dynOffsets.count()), dynOffsets.data());
}

inline void VkCommandEncoderImpl::setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) {
    if (!currentRtPipeline_ || !currentRtPipeline_->vkLayout()) return;
    vkCmdPushConstants(cmdBuf_, currentRtPipeline_->vkLayout()->handle(),
        toVkShaderStageFlags(stages), offset, size, data);
}

inline void VkCommandEncoderImpl::traceRays(
    Buffer* raygenSBT, u64 raygenOff, u64 raygenStride,
    Buffer* missSBT, u64 missOff, u64 missStride,
    Buffer* hitSBT, u64 hitOff, u64 hitStride, u32 w, u32 h, u32 d) {
    auto* rg = static_cast<VkBufferImpl*>(raygenSBT);
    if (!rg) return;
    VkStridedDeviceAddressRegionKHR rgn{}, msn{}, htn{}, cal{};
    rgn.deviceAddress = getBufferDeviceAddress(rg) + raygenOff; rgn.stride = raygenStride; rgn.size = raygenStride;
    if (auto* ms = static_cast<VkBufferImpl*>(missSBT)) {
        msn.deviceAddress = getBufferDeviceAddress(ms) + missOff; msn.stride = missStride; msn.size = missStride;
    }
    if (auto* ht = static_cast<VkBufferImpl*>(hitSBT)) {
        htn.deviceAddress = getBufferDeviceAddress(ht) + hitOff; htn.stride = hitStride; htn.size = hitStride;
    }
    if (!pfnTrace_) pfnTrace_ = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device_, "vkCmdTraceRaysKHR"));
    if (pfnTrace_) pfnTrace_(cmdBuf_, &rgn, &msn, &htn, &cal, w, h, d);
}

inline void VkCommandEncoderImpl::buildBottomLevelAccelStruct(
    AccelStruct* dst, Buffer* scratch, u64 scratchOffset,
    Span<const AccelStructGeometryTriangles> tris, Span<const AccelStructGeometryAABBs> aabbs) {
    auto* as = static_cast<VkAccelStructImpl*>(dst);
    auto* sc = static_cast<VkBufferImpl*>(scratch);
    if (!as || !sc) return;

    usize total = tris.count() + aabbs.count();
    std::vector<VkAccelerationStructureGeometryKHR> geoms(total);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(total);
    usize idx = 0;

    for (usize i = 0; i < tris.count(); ++i) {
        const auto& t = tris[i];
        auto* vb = static_cast<VkBufferImpl*>(t.vertexBuffer); if (!vb) continue;
        VkAccelerationStructureGeometryTrianglesDataKHR td{};
        td.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        td.vertexFormat = toVkVertexFormat(t.vertexFormat);
        td.vertexData.deviceAddress = getBufferDeviceAddress(vb) + t.vertexOffset;
        td.vertexStride = t.vertexStride; td.maxVertex = t.vertexCount - 1;
        if (auto* ib = static_cast<VkBufferImpl*>(t.indexBuffer)) {
            td.indexType = toVkIndexType(t.indexFormat);
            td.indexData.deviceAddress = getBufferDeviceAddress(ib) + t.indexOffset;
        } else td.indexType = VK_INDEX_TYPE_NONE_KHR;
        if (auto* tb = static_cast<VkBufferImpl*>(t.transformBuffer))
            td.transformData.deviceAddress = getBufferDeviceAddress(tb) + t.transformOffset;
        geoms[idx] = {}; geoms[idx].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geoms[idx].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR; geoms[idx].geometry.triangles = td;
        { VkGeometryFlagsKHR gf = 0;
          if (static_cast<u32>(t.flags) & static_cast<u32>(GeometryFlags::Opaque)) gf |= VK_GEOMETRY_OPAQUE_BIT_KHR;
          if (static_cast<u32>(t.flags) & static_cast<u32>(GeometryFlags::NoDuplicateAnyHitInvocation)) gf |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
          geoms[idx].flags = gf; }
        ranges[idx] = {}; ranges[idx].primitiveCount = t.indexBuffer ? t.indexCount / 3 : t.vertexCount / 3;
        ++idx;
    }
    for (usize i = 0; i < aabbs.count(); ++i) {
        const auto& a = aabbs[i];
        auto* ab = static_cast<VkBufferImpl*>(a.aabbBuffer); if (!ab) continue;
        VkAccelerationStructureGeometryAabbsDataKHR ad{};
        ad.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        ad.data.deviceAddress = getBufferDeviceAddress(ab) + a.offset; ad.stride = a.stride;
        geoms[idx] = {}; geoms[idx].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geoms[idx].geometryType = VK_GEOMETRY_TYPE_AABBS_KHR; geoms[idx].geometry.aabbs = ad;
        { VkGeometryFlagsKHR gf = 0;
          if (static_cast<u32>(a.flags) & static_cast<u32>(GeometryFlags::Opaque)) gf |= VK_GEOMETRY_OPAQUE_BIT_KHR;
          if (static_cast<u32>(a.flags) & static_cast<u32>(GeometryFlags::NoDuplicateAnyHitInvocation)) gf |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
          geoms[idx].flags = gf; }
        ranges[idx] = {}; ranges[idx].primitiveCount = a.count;
        ++idx;
    }

    VkAccelerationStructureBuildGeometryInfoKHR bi{};
    bi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.dstAccelerationStructure = as->handle();
    bi.geometryCount = static_cast<u32>(idx); bi.pGeometries = geoms.data();
    bi.scratchData.deviceAddress = getBufferDeviceAddress(sc) + scratchOffset;

    if (!pfnBuild_) pfnBuild_ = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device_, "vkCmdBuildAccelerationStructuresKHR"));
    if (!pfnBuild_) return;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = ranges.data();
    pfnBuild_(cmdBuf_, 1, &bi, &pRange);
}

inline void VkCommandEncoderImpl::buildTopLevelAccelStruct(
    AccelStruct* dst, Buffer* scratch, u64 scratchOffset,
    Buffer* instanceBuf, u64 instanceOffset, u32 instanceCount) {
    auto* as = static_cast<VkAccelStructImpl*>(dst);
    auto* sc = static_cast<VkBufferImpl*>(scratch);
    auto* ib = static_cast<VkBufferImpl*>(instanceBuf);
    if (!as || !sc || !ib) return;

    VkAccelerationStructureGeometryInstancesDataKHR id{};
    id.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    id.data.deviceAddress = getBufferDeviceAddress(ib) + instanceOffset;
    VkAccelerationStructureGeometryKHR geom{}; geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR; geom.geometry.instances = id;

    VkAccelerationStructureBuildGeometryInfoKHR bi{};
    bi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.dstAccelerationStructure = as->handle();
    bi.geometryCount = 1; bi.pGeometries = &geom;
    bi.scratchData.deviceAddress = getBufferDeviceAddress(sc) + scratchOffset;

    VkAccelerationStructureBuildRangeInfoKHR range{}; range.primitiveCount = instanceCount;
    if (!pfnBuild_) pfnBuild_ = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device_, "vkCmdBuildAccelerationStructuresKHR"));
    if (!pfnBuild_) return;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
    pfnBuild_(cmdBuf_, 1, &bi, &pRange);
}

// ---- Deferred mesh shader method implementations on RenderPassEncoder ----

VkPipelineLayoutImpl* VkRenderPassEncoderImpl::getCurrentLayout() {
    if (currentPipeline_)     return currentPipeline_->vkLayout();
    if (currentMeshPipeline_) return currentMeshPipeline_->vkLayout();
    return nullptr;
}

void VkRenderPassEncoderImpl::setMeshPipeline(MeshPipeline* pipeline) {
    currentMeshPipeline_ = static_cast<VkMeshPipelineImpl*>(pipeline);
    currentPipeline_     = nullptr;
    if (currentMeshPipeline_)
        vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS, currentMeshPipeline_->handle());
}

void VkRenderPassEncoderImpl::drawMeshTasks(u32 gx, u32 gy, u32 gz) {
    if (!pfnDrawMesh_) pfnDrawMesh_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
        vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksEXT"));
    if (pfnDrawMesh_) pfnDrawMesh_(cmdBuf_, gx, gy, gz);
}

void VkRenderPassEncoderImpl::drawMeshTasksIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) {
    auto* vkBuf = static_cast<VkBufferImpl*>(buf); if (!vkBuf) return;
    if (!pfnDrawMeshIndirect_) pfnDrawMeshIndirect_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(
        vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksIndirectEXT"));
    if (pfnDrawMeshIndirect_) pfnDrawMeshIndirect_(cmdBuf_, vkBuf->handle(), offset, drawCount, stride > 0 ? stride : 12);
}

void VkRenderPassEncoderImpl::drawMeshTasksIndirectCount(
    Buffer* buf, u64 offset, Buffer* countBuf, u64 countOffset, u32 maxDrawCount, u32 stride) {
    auto* vkBuf = static_cast<VkBufferImpl*>(buf);
    auto* vkCb  = static_cast<VkBufferImpl*>(countBuf);
    if (!vkBuf || !vkCb) return;
    if (!pfnDrawMeshIndCount_) pfnDrawMeshIndCount_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(
        vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksIndirectCountEXT"));
    if (pfnDrawMeshIndCount_) pfnDrawMeshIndCount_(cmdBuf_, vkBuf->handle(), offset,
        vkCb->handle(), countOffset, maxDrawCount, stride > 0 ? stride : 12);
}

// ---- CommandPool deferred implementations ----

Status VkCommandPoolImpl::createEncoder(CommandEncoder*& out) {
    out = nullptr;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;

    if (!freeHandles_.empty()) {
        cmdBuf = freeHandles_.back(); freeHandles_.pop_back();
    } else {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = pool_;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &ai, &cmdBuf) != VK_SUCCESS) return ErrorCode::Unknown;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &bi);

    out = new VkCommandEncoderImpl(cmdBuf, device_, this);
    return ErrorCode::Ok;
}

void VkCommandPoolImpl::destroyEncoder(CommandEncoder*& encoder) {
    if (encoder) { delete encoder; encoder = nullptr; }
}

void VkCommandPoolImpl::reset() {
    for (auto* cb : trackedBuffers_) { freeHandles_.push_back(cb->handle()); delete cb; }
    trackedBuffers_.clear();
    vkResetCommandPool(device_, pool_, 0);
}

} // namespace raptor::rhi::vk
