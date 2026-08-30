// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Forward declarations for all RHI types.

export module foundation.rhi:forward;

export namespace foundation::rhi
{

    class Backend;
    class Adapter;
    class Device;
    class Queue;
    class Surface;
    class SwapChain;
    class Buffer;
    class Texture;
    class TextureView;
    class Sampler;
    class ShaderModule;
    class Fence;
    class QuerySet;
    class BindGroupLayout;
    class BindGroup;
    class PipelineLayout;
    class PipelineCache;
    class RenderPipeline;
    class ComputePipeline;
    class MeshPipeline;
    class AccelStruct;
    class RayTracingPipeline;
    class CommandBuffer;
    class CommandPool;
    class CommandEncoder;
    class RenderCommandEncoder;
    class RenderPassEncoder;
    class RenderBundleEncoder;
    class RenderBundle;
    class ComputePassEncoder;
    class TransferBatch;

    // Extension base classes (for multiple inheritance).
    class MeshShaderPassExt;
    class RayTracingEncoderExt;

} // namespace foundation::rhi
