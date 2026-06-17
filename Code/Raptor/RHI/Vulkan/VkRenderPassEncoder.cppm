/// Vulkan implementation of RenderPassEncoder + MeshShaderPassExt.
/// Ported from Sedulous.RHI.Vulkan/VulkanRenderPassEncoder.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

export module raptor.rhi.vk:render_pass_encoder;

import raptor.core;
import raptor.rhi;
import :conversions;
import :buffer;
import :bind_group;
import :render_pipeline;
import :compute_pipeline;
import :pipeline_layout;
import :query_set;
import :mesh_pipeline;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkRenderPassEncoderImpl : public RenderPassEncoder, public MeshShaderPassExt {
public:
    VkRenderPassEncoderImpl(VkCommandBuffer cmdBuf, VkDevice device)
        : cmdBuf_(cmdBuf), device_(device) {}

    // ---- RenderPassEncoder ----

    void setPipeline(RenderPipeline* pipeline) override {
        currentPipeline_ = static_cast<VkRenderPipelineImpl*>(pipeline);
        currentMeshPipeline_ = nullptr;
        if (currentPipeline_)
            vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline_->handle());
    }

    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        auto* bg = static_cast<VkBindGroupImpl*>(group);
        auto* layout = getCurrentLayout();
        if (!bg || !layout) return;
        VkDescriptorSet set = bg->handle();
        vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle(),
            index, 1, &set, static_cast<u32>(dynOffsets.Size()), dynOffsets.Data());
    }

    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        auto* layout = getCurrentLayout();
        if (!layout) return;
        vkCmdPushConstants(cmdBuf_, layout->handle(), toVkShaderStageFlags(stages), offset, size, data);
    }

    void setVertexBuffer(u32 slot, Buffer* buffer, u64 offset) override {
        auto* vkBuf = static_cast<VkBufferImpl*>(buffer);
        if (!vkBuf) return;
        VkBuffer handle = vkBuf->handle();
        vkCmdBindVertexBuffers(cmdBuf_, slot, 1, &handle, &offset);
    }

    void setIndexBuffer(Buffer* buffer, IndexFormat format, u64 offset) override {
        auto* vkBuf = static_cast<VkBufferImpl*>(buffer);
        if (!vkBuf) return;
        vkCmdBindIndexBuffer(cmdBuf_, vkBuf->handle(), offset, toVkIndexType(format));
    }

    void setViewport(f32 x, f32 y, f32 w, f32 h, f32 minDepth, f32 maxDepth) override {
        // Flip Y via negative height to match DX12 coordinate system.
        VkViewport vp{}; vp.x = x; vp.y = y + h; vp.width = w; vp.height = -h;
        vp.minDepth = minDepth; vp.maxDepth = maxDepth;
        vkCmdSetViewport(cmdBuf_, 0, 1, &vp);
    }

    void setScissor(i32 x, i32 y, u32 w, u32 h) override {
        VkRect2D sc{}; sc.offset = {x, y}; sc.extent = {w, h};
        vkCmdSetScissor(cmdBuf_, 0, 1, &sc);
    }

    void setBlendConstant(f32 r, f32 g, f32 b, f32 a) override {
        f32 c[4] = {r, g, b, a};
        vkCmdSetBlendConstants(cmdBuf_, c);
    }

    void setStencilReference(u32 ref) override {
        vkCmdSetStencilReference(cmdBuf_, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        vkCmdDraw(cmdBuf_, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex, u32 firstInstance) override {
        vkCmdDrawIndexed(cmdBuf_, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void drawIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) override {
        auto* vkBuf = static_cast<VkBufferImpl*>(buf);
        if (!vkBuf) return;
        vkCmdDrawIndirect(cmdBuf_, vkBuf->handle(), offset, drawCount, stride > 0 ? stride : 16);
    }

    void drawIndexedIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) override {
        auto* vkBuf = static_cast<VkBufferImpl*>(buf);
        if (!vkBuf) return;
        vkCmdDrawIndexedIndirect(cmdBuf_, vkBuf->handle(), offset, drawCount, stride > 0 ? stride : 20);
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        auto* q = static_cast<VkQuerySetImpl*>(qs);
        if (q) vkCmdWriteTimestamp(cmdBuf_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q->handle(), index);
    }

    void beginOcclusionQuery(QuerySet* qs, u32 index) override {
        auto* q = static_cast<VkQuerySetImpl*>(qs);
        if (q) vkCmdBeginQuery(cmdBuf_, q->handle(), index, 0);
    }

    void endOcclusionQuery(QuerySet* qs, u32 index) override {
        auto* q = static_cast<VkQuerySetImpl*>(qs);
        if (q) vkCmdEndQuery(cmdBuf_, q->handle(), index);
    }

    void end() override {
        vkCmdEndRendering(cmdBuf_);
        currentPipeline_ = nullptr;
        currentMeshPipeline_ = nullptr;
    }

    // ---- MeshShaderPassExt ----

    void setMeshPipeline(MeshPipeline* pipeline) override;
    void drawMeshTasks(u32 gx, u32 gy, u32 gz) override;
    void drawMeshTasksIndirect(Buffer* buf, u64 offset, u32 drawCount, u32 stride) override;
    void drawMeshTasksIndirectCount(Buffer* buf, u64 offset, Buffer* countBuf, u64 countOffset, u32 maxDrawCount, u32 stride) override;

private:
    VkPipelineLayoutImpl* getCurrentLayout();

    VkCommandBuffer       cmdBuf_ = VK_NULL_HANDLE;
    VkDevice              device_ = VK_NULL_HANDLE;
    VkRenderPipelineImpl* currentPipeline_     = nullptr;
    VkMeshPipelineImpl*   currentMeshPipeline_ = nullptr;

    // Cached device-level mesh shader function pointers.
    PFN_vkCmdDrawMeshTasksEXT              pfnDrawMesh_          = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectEXT      pfnDrawMeshIndirect_  = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnDrawMeshIndCount_  = nullptr;
};

} // namespace raptor::rhi::vk
