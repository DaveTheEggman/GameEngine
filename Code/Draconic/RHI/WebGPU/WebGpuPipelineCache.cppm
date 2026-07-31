/// draconic.rhi.webgpu:pipeline_cache - PipelineCache stand-in.
///
/// WebGPU has no pipeline-cache object (the browser/driver caches internally), and
/// the RHI contract treats caches as best-effort - so creation succeeds with an
/// empty cache: pipelines simply ignore it, GetData serves zero bytes.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.rhi.webgpu:pipeline_cache;

import draconic.core;
import draconic.rhi;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuPipelineCache final : public PipelineCache
    {
    public:
        u32 GetDataSize() override { return 0; }
        Status GetData(Span<u8>) override { return ErrorCode::Ok; }
    };
}
