/// Draconic::Render - the `:cluster_system` partition.
///
/// Clustered light culling (phase 4.3). Bins lights into a 3D froxel grid (screen tiles ×
/// logarithmic depth slices) once per frame via a compute pass, so the forward shader evaluates
/// only the lights touching each fragment's cluster instead of all lights. This partition owns the
/// build compute pipeline + the per-frame cluster buffers, and declares the build pass into the
/// frame graph (ordered before the forward pass, which reads the buffers it writes).
///
/// 4.3a establishes the plumbing with a STUB kernel (writes empty cluster lists); 4.3b ports the
/// real froxel-AABB / sphere-assignment kernel; 4.3c wires the buffers into the forward shader.

module;
#include "Core/Prelude.h"

export module draconic.render:cluster_system;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;
import :views;
import :resources;
import :cluster_shaders; // ClusterBuildCS() - HLSL split into ClusterShaders.cppm

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render
{

    // What a built cluster grid exposes to the forward pass (the buffers + the grid params the
    // fragment shader needs to map a pixel+depth to its cluster). Filled by DeclareBuild.
    struct ClusterBinding
    {
        rhi::Buffer* offsets = nullptr;      // uint2 per cluster: (indexStart, count)
        rhi::Buffer* lightIndices = nullptr; // flat uint light-index list
        // Bumped every time this slot's buffers are reallocated (resize). Consumers must invalidate any
        // cached bind group on a version change - a freed rhi::Buffer* address can be REUSED by the new
        // allocation, so pointer-equality is NOT a reliable "unchanged" test (use-after-free otherwise).
        u32 version = 0;
        rendergraph::RGHandle offsetsHandle =
            {}; // graph handles so the forward pass can ReadBuffer them
        rendergraph::RGHandle indicesHandle =
            {}; // (orders the build write before the shading read)
        u32 gridX = 0, gridY = 0, sliceCount = 0, tileSize = 0;
        i32 viewportX = 0,
            viewportY = 0; // grid is viewport-local; forward subtracts this from SV_Position
        f32 nearZ = 0.0f, farZ = 0.0f, logScale = 0.0f, logBias = 0.0f;
        [[nodiscard]] bool Valid() const noexcept
        {
            return offsets != nullptr && lightIndices != nullptr;
        }
    };

    class ClusterSystem
    {
    public:
        ClusterSystem(rhi::Device& device, shaders::ShaderSystem& shaders,
                      u32 framesInFlight) noexcept
            : m_device(&device), m_shaders(&shaders),
              m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight),
              m_paramsRing(device, m_framesInFlight, kParamsSlot,
                           rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst,
                           u8"cluster.params"),
              m_lightRing(device, m_framesInFlight, sizeof(GpuLight),
                          rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"cluster.lights")
        {
        }

        ~ClusterSystem() { Shutdown(); }
        ClusterSystem(const ClusterSystem&) = delete;
        ClusterSystem& operator=(const ClusterSystem&) = delete;

        // Build the compute pipeline + its layout. Returns non-Ok if the shader/pipeline fails.
        Status Initialize()
        {
            m_shaders->RegisterSource(u8"cluster_build", shaders::ShaderStage::Compute,
                                      ClusterBuildCS());
            rhi::ShaderModule* cs = m_shaders->GetVariant(
                u8"cluster_build", shaders::ShaderStage::Compute, shaders::ShaderFlags::None);
            if (cs == nullptr)
            {
                return Status{ErrorCode::Unknown};
            }

            // set 0: build params UBO (b0, dynamic) + lights SRV (t0) + per-cluster offsets (u0) +
            // flat light-index list (u1). DXC shifts keep b0/t0/u0/u1 from colliding in SPIR-V.
            rhi::BindGroupLayoutEntry paramsEntry =
                rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Compute);
            paramsEntry.hasDynamicOffset = true;
            rhi::BindGroupLayoutEntry lightsEntry = rhi::BindGroupLayoutEntry::StorageBuffer(
                0, rhi::ShaderStage::Compute, /*readOnly*/ true);
            rhi::BindGroupLayoutEntry offsetsEntry = rhi::BindGroupLayoutEntry::StorageBuffer(
                0, rhi::ShaderStage::Compute, /*readOnly*/ false);
            rhi::BindGroupLayoutEntry indicesEntry = rhi::BindGroupLayoutEntry::StorageBuffer(
                1, rhi::ShaderStage::Compute, /*readOnly*/ false);
            rhi::BindGroupLayoutEntry set0[] = {paramsEntry, lightsEntry, offsetsEntry,
                                                indicesEntry};
            rhi::BindGroupLayoutDesc ld{};
            ld.entries = Span<const rhi::BindGroupLayoutEntry>{set0, 4};
            if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk())
            {
                return Status{ErrorCode::Unknown};
            }

            rhi::BindGroupLayout* layouts[] = {m_layout};
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{layouts, 1};
            if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk())
            {
                return Status{ErrorCode::Unknown};
            }

            rhi::ComputePipelineDesc cpd{};
            cpd.layout = m_pipelineLayout;
            cpd.compute = rhi::ProgrammableStage{cs, u8"main", rhi::ShaderStage::Compute};
            cpd.label = u8"cluster.build";
            if (!m_device->CreateComputePipeline(cpd, m_pipeline).IsOk())
            {
                return Status{ErrorCode::Unknown};
            }
            return Status{};
        }

        // Size the params ring for this frame + select its region. Call once per frame before any view.
        void PrepareFrame(u32 frameIndex)
        {
            m_ready = m_paramsRing.Reserve(kMaxViewsPerFrame) &&
                      m_lightRing.Reserve(kMaxLights * kMaxViewsPerFrame);
            if (m_ready)
            {
                m_paramsRing.BeginFrame(frameIndex);
                m_lightRing.BeginFrame(frameIndex);
            }
        }

        // Declare this view's cluster build compute pass into the graph (ordered before the forward
        // pass that reads the cluster buffers). Returns the binding the forward pass consumes, or an
        // empty binding if unavailable. The cluster grid is derived from the view's resolution + camera.
        ClusterBinding DeclareBuild(rendergraph::RenderGraph& graph, const RenderView& view,
                                    u32 frameIndex, u32 viewIndex)
        {
            ClusterBinding binding;
            if (!m_ready || m_pipeline == nullptr || view.Width() == 0 || view.Height() == 0)
            {
                return binding;
            }
            if (viewIndex >= kMaxViewsPerFrame)
            {
                return binding;
            } // beyond budget -> all-lights fallback

            // Grid covers the VIEWPORT (not the full target) in viewport-local screen space; the forward
            // subtracts the viewport offset from SV_Position before tiling.
            const u32 gridX = (view.ViewportWidth() + kTileSize - 1) / kTileSize;
            const u32 gridY = (view.ViewportHeight() + kTileSize - 1) / kTileSize;
            const u32 totalClusters = gridX * gridY * kSliceCount;
            if (totalClusters == 0)
            {
                return binding;
            }

            // Own buffers per (view, frame-in-flight): two views in one frame must not share a slot.
            const u32 bufferSlot = viewIndex * m_framesInFlight + (frameIndex % m_framesInFlight);
            if (!EnsureBuffers(bufferSlot, totalClusters))
            {
                return binding;
            }

            const f32 nearZ =
                0.1f; // ViewCamera carries only farZ; near matches the CameraComponent default
            const f32 farZ = (view.Camera().farZ > 0.0f) ? view.Camera().farZ : 1000.0f;
            // slice = SliceCount * log(depth/near) / log(far/near) -> inverse of GetSliceDepth in the
            // kernel. The SliceCount factor is essential: without it every depth collapses to slice 0.
            const f32 logScale = static_cast<f32>(kSliceCount) / Log(farZ / nearZ);
            const f32 logBias = -Log(nearZ) * logScale;

            // Upload this view's lights into the cluster light buffer (its own copy - the build runs
            // before the forward uploads its lights; the stored indices are valid for both, same order).
            const Span<const GpuLight> lights =
                (view.Scene() != nullptr) ? view.Scene()->Lights() : Span<const GpuLight>{};
            u32 lightCount = static_cast<u32>(lights.Size());
            if (lightCount > kMaxLights)
            {
                lightCount = kMaxLights;
            }
            u32 lightOffset = 0;
            if (lightCount > 0)
            {
                const DynamicUniformRing::Range lr = m_lightRing.AllocateRange(lightCount);
                if (!lr.ok)
                {
                    return binding;
                }
                MemCopy(lr.ptr, lights.Data(), static_cast<usize>(lightCount) * sizeof(GpuLight));
                lightOffset = lr.slotIndex;
            }

            // Upload this view's build params (a dynamic-offset slot in the params ring).
            const DynamicUniformRing::Range pr = m_paramsRing.Allocate();
            if (!pr.ok)
            {
                return binding;
            }
            BuildParams* bp = static_cast<BuildParams*>(pr.ptr);
            *bp = BuildParams{};
            bp->gridX = gridX;
            bp->gridY = gridY;
            bp->sliceCount = kSliceCount;
            bp->tileSize = kTileSize;
            bp->nearZ = nearZ;
            bp->farZ = farZ;
            bp->logScale = logScale;
            bp->logBias = logBias;
            bp->lightCount = lightCount;
            bp->lightOffset = lightOffset;
            bp->viewMatrix = view.Camera().view;
            bp->invProjection = Inverse(view.Camera().projection);

            rhi::Buffer* offsets = m_offsets[bufferSlot];
            rhi::Buffer* indices = m_indices[bufferSlot];
            if (!EnsureBindGroup(bufferSlot, offsets, indices))
            {
                return binding;
            }

            const u32 paramsOffset = pr.byteOffset;
            const u32 groups = (totalClusters + 63u) / 64u;
            rhi::BindGroup* bg = m_bindGroups[bufferSlot];
            rhi::ComputePipeline* pipeline = m_pipeline;

            const rendergraph::RGHandle offsetsH = graph.ImportBuffer(u8"cluster.offsets", offsets);
            const rendergraph::RGHandle indicesH = graph.ImportBuffer(u8"cluster.indices", indices);
            graph.AddComputePass(
                u8"cluster.build",
                [=](rendergraph::PassBuilder& b)
                {
                    b.WriteStorage(offsetsH);
                    b.WriteStorage(indicesH);
                    b.HasSideEffects(); // until the forward pass reads the buffers (4.3c), keep the pass alive
                    b.SetComputeExecute(
                        [=](rhi::ComputePassEncoder& cp)
                        {
                            cp.SetPipeline(pipeline);
                            cp.SetBindGroup(0, bg, Span<const u32>{&paramsOffset, 1});
                            cp.Dispatch(groups, 1, 1);
                        });
                });

            binding.offsets = offsets;
            binding.lightIndices = indices;
            binding.version = m_bufferVersion[bufferSlot];
            binding.offsetsHandle = offsetsH;
            binding.indicesHandle = indicesH;
            binding.gridX = gridX;
            binding.gridY = gridY;
            binding.sliceCount = kSliceCount;
            binding.tileSize = kTileSize;
            binding.viewportX = view.ViewportX();
            binding.viewportY = view.ViewportY();
            binding.nearZ = nearZ;
            binding.farZ = farZ;
            binding.logScale = logScale;
            binding.logBias = logBias;
            return binding;
        }

    private:
        // Matches the ClusterBuildParams cbuffer (std140; 16-aligned scalars + two row-major mats).
        struct BuildParams
        {
            u32 gridX = 0, gridY = 0, sliceCount = 0, tileSize = 0;
            f32 nearZ = 0.0f, farZ = 0.0f, logScale = 0.0f, logBias = 0.0f;
            u32 lightCount = 0, lightOffset = 0;
            f32 pad0 = 0.0f, pad1 = 0.0f;
            Float4x4 viewMatrix;
            Float4x4 invProjection;
        };

        static constexpr u32 kTileSize = 16;
        static constexpr u32 kSliceCount = 24;
        static constexpr u32 kMaxPerCluster =
            64; // per-cluster light cap (matches MAX_PER_CLUSTER in the kernel)
        static constexpr u32 kMaxLights = 256; // per-view light budget (matches the forward's)
        static constexpr u64 kParamsSlot =
            256; // dynamic UBO offset alignment (>= sizeof(BuildParams))
        static constexpr u32 kMaxViewsPerFrame = 8;

        // (Re)create one (view,frame) slot's cluster buffers when its cluster count grows. Lazy: only
        // the slots actually used by rendered views are allocated.
        bool EnsureBuffers(u32 bufferSlot, u32 totalClusters)
        {
            const u64 offsetsBytes =
                static_cast<u64>(totalClusters) * sizeof(u32) * 2; // uint2 per cluster
            const u64 indicesBytes =
                static_cast<u64>(totalClusters) * kMaxPerCluster * sizeof(u32); // flat index list
            if (m_offsets[bufferSlot] != nullptr && offsetsBytes <= m_offsetsBytes[bufferSlot])
            {
                return true;
            }

            // Grow this slot (GPU idle at startup; resolution changes are rare).
            m_device->WaitIdle();
            if (m_offsets[bufferSlot] != nullptr)
            {
                m_device->DestroyBuffer(m_offsets[bufferSlot]);
                m_offsets[bufferSlot] = nullptr;
            }
            if (m_indices[bufferSlot] != nullptr)
            {
                m_device->DestroyBuffer(m_indices[bufferSlot]);
                m_indices[bufferSlot] = nullptr;
            }
            rhi::BufferDesc obd{};
            obd.size = offsetsBytes;
            obd.usage = rhi::BufferUsage::Storage;
            obd.memory = rhi::MemoryLocation::GpuOnly;
            obd.label = u8"cluster.offsets";
            if (!m_device->CreateBuffer(obd, m_offsets[bufferSlot]).IsOk())
            {
                m_offsets[bufferSlot] = nullptr;
                return false;
            }
            rhi::BufferDesc ibd{};
            ibd.size = indicesBytes;
            ibd.usage = rhi::BufferUsage::Storage;
            ibd.memory = rhi::MemoryLocation::GpuOnly;
            ibd.label = u8"cluster.indices";
            if (!m_device->CreateBuffer(ibd, m_indices[bufferSlot]).IsOk())
            {
                m_indices[bufferSlot] = nullptr;
                return false;
            }
            m_offsetsBytes[bufferSlot] = offsetsBytes;
            m_indicesBytes[bufferSlot] = indicesBytes;
            ++m_bufferVersion
                [bufferSlot]; // signal consumers to rebuild cached bind groups (address may reuse)
            // The slot's bind group referenced the old buffers - drop it (GPU idle after WaitIdle).
            if (m_bindGroups[bufferSlot] != nullptr)
            {
                m_device->DestroyBindGroup(m_bindGroups[bufferSlot]);
                m_bindGroups[bufferSlot] = nullptr;
            }
            m_bgOffsets[bufferSlot] = nullptr;
            return true;
        }

        // (Re)build the bind group for a (view,frame) slot. One bind group PER slot (each over its own
        // buffers) so a slot's group is stable across frames - never freed while the previous frame's
        // command buffer that referenced it is still in flight.
        bool EnsureBindGroup(u32 bufferSlot, rhi::Buffer* offsets, rhi::Buffer* indices)
        {
            rhi::Buffer* lights = m_lightRing.Buffer();
            const bool stable = m_bindGroups[bufferSlot] != nullptr &&
                                m_bgParamsGen[bufferSlot] == m_paramsRing.Generation() &&
                                m_bgLightGen[bufferSlot] == m_lightRing.Generation() &&
                                m_bgOffsets[bufferSlot] == offsets;
            if (stable)
            {
                return true;
            }
            if (m_bindGroups[bufferSlot] != nullptr)
            {
                m_device->DestroyBindGroup(m_bindGroups[bufferSlot]);
                m_bindGroups[bufferSlot] = nullptr;
            }
            rhi::Buffer* params = m_paramsRing.Buffer();
            if (params == nullptr || lights == nullptr || offsets == nullptr || indices == nullptr)
            {
                return false;
            }
            rhi::BindGroupEntry entries[] = {
                rhi::BindGroupEntry::BufferEntry(params, 0, sizeof(BuildParams)),
                rhi::BindGroupEntry::BufferEntry(lights, 0, m_lightRing.ByteCapacity()),
                rhi::BindGroupEntry::BufferEntry(offsets, 0, m_offsetsBytes[bufferSlot]),
                rhi::BindGroupEntry::BufferEntry(indices, 0, m_indicesBytes[bufferSlot]),
            };
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_layout;
            bgd.entries = Span<const rhi::BindGroupEntry>{entries, 4};
            if (!m_device->CreateBindGroup(bgd, m_bindGroups[bufferSlot]).IsOk())
            {
                m_bindGroups[bufferSlot] = nullptr;
                return false;
            }
            m_bgParamsGen[bufferSlot] = m_paramsRing.Generation();
            m_bgLightGen[bufferSlot] = m_lightRing.Generation();
            m_bgOffsets[bufferSlot] = offsets;
            return true;
        }

        void Shutdown()
        {
            for (u32 i = 0; i < kMaxBufferSlots; ++i)
            {
                if (m_bindGroups[i] != nullptr)
                {
                    m_device->DestroyBindGroup(m_bindGroups[i]);
                    m_bindGroups[i] = nullptr;
                }
                if (m_offsets[i] != nullptr)
                {
                    m_device->DestroyBuffer(m_offsets[i]);
                    m_offsets[i] = nullptr;
                }
                if (m_indices[i] != nullptr)
                {
                    m_device->DestroyBuffer(m_indices[i]);
                    m_indices[i] = nullptr;
                }
            }
            if (m_pipeline != nullptr)
            {
                m_device->DestroyComputePipeline(m_pipeline);
                m_pipeline = nullptr;
            }
            if (m_pipelineLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }
            if (m_layout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_layout);
                m_layout = nullptr;
            }
        }

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;
        u32 m_framesInFlight = 2;

        rhi::BindGroupLayout* m_layout = nullptr;
        rhi::PipelineLayout* m_pipelineLayout = nullptr;
        rhi::ComputePipeline* m_pipeline = nullptr;

        static constexpr u32 kMaxFramesInFlight = 8;
        // Cluster buffers are owned per (view, frame-in-flight) slot - two views in one frame must not
        // share a buffer (the Sedulous "two pipelines stomp the same frameIndex%2 slot" bug). Slots are
        // allocated LAZILY (the indices buffer is large), so unused view slots cost only a null pointer.
        static constexpr u32 kMaxBufferSlots = kMaxViewsPerFrame * kMaxFramesInFlight;

        DynamicUniformRing m_paramsRing;
        DynamicUniformRing
            m_lightRing; // ClusterSystem's own light copy (built before the forward uploads its)
        rhi::Buffer* m_offsets[kMaxBufferSlots] = {}; // per-(view,frame) cluster (offset,count)
        rhi::Buffer* m_indices[kMaxBufferSlots] = {}; // per-(view,frame) flat light-index list
        u64 m_offsetsBytes[kMaxBufferSlots] = {}; // size of each slot's buffers (views can differ)
        u64 m_indicesBytes[kMaxBufferSlots] = {};
        u32 m_bufferVersion[kMaxBufferSlots] =
            {}; // ++ on realloc; consumers invalidate cached bind groups

        // One bind group per (view, frame) slot (each over its own buffers), so a slot's group is never
        // freed while still referenced by an in-flight frame.
        rhi::BindGroup* m_bindGroups[kMaxBufferSlots] = {};
        u32 m_bgParamsGen[kMaxBufferSlots] = {};
        u32 m_bgLightGen[kMaxBufferSlots] = {};
        rhi::Buffer* m_bgOffsets[kMaxBufferSlots] = {};
        bool m_ready = false;
    };

} // namespace draconic::render
