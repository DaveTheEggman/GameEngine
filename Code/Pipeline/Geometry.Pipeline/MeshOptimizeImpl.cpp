// Pipeline::Geometry - implementation unit for the P0 mesh optimization pass
// (Documentation/Specs/mesh-lod.md).
//
// meshoptimizer is included HERE only - the interface stays clean of the vendored
// header (the module-interface hygiene rule). The pass:
//   1. per TRIANGLE submesh: meshopt_optimizeVertexCache then meshopt_optimizeOverdraw
//      (in place on that submesh's index range; ranges themselves never move);
//   2. whole mesh: meshopt_optimizeVertexFetchRemap -> remap the vertex blob (compacting
//      vertices no index references) + rewrite EVERY index (non-triangle ranges too, so
//      shared vertices stay coherent).
// Everything is a pure reorder: triangle set, winding, submesh ranges, and vertex values
// are unchanged - rendering output is identical, fetch/cache behavior is better.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

#include <meshoptimizer.h>

module geometry.pipeline;

import foundation.core;
import foundation.geometry;
import foundation.geometry.resource;

using namespace foundation::core;
using namespace foundation::geometry;

namespace pipeline
{
    namespace
    {
        // GPU-cache ACMR over every triangle range (the number the cook log + tests
        // watch; 16-entry FIFO is meshoptimizer's recommended modern-GPU model).
        [[nodiscard]] f32 TriangleAcmr(const StaticMeshSource& source, usize vertexCount)
        {
            f32 weighted = 0.0f;
            usize triangles = 0;
            for (usize i = 0; i < source.subStart.Size(); ++i)
            {
                const u8 prim = (i < source.subPrim.Size()) ? source.subPrim[i] : 0;
                const i32 count = source.subCount[i];
                if (static_cast<PrimitiveType>(prim) != PrimitiveType::Triangles || count < 3)
                {
                    continue;
                }
                const meshopt_VertexCacheStatistics stats = meshopt_analyzeVertexCache(
                    source.indexData.Data() + source.subStart[i], static_cast<usize>(count),
                    vertexCount, 16, 0, 0);
                weighted += stats.acmr * static_cast<f32>(count / 3);
                triangles += static_cast<usize>(count / 3);
            }
            return (triangles > 0) ? weighted / static_cast<f32>(triangles) : 0.0f;
        }
    }

    void OptimizeStaticMeshSource(StaticMeshSource& source, MeshOptimizeStats* outStats)
    {
        constexpr usize kStride = sizeof(StaticMeshVertex);
        const usize vertexCount = source.vertexBlob.Size() / kStride;
        MeshOptimizeStats stats;
        stats.verticesBefore = static_cast<u32>(vertexCount);
        stats.verticesAfter = static_cast<u32>(vertexCount);
        if (vertexCount == 0 || source.indexData.IsEmpty() || source.subStart.IsEmpty())
        {
            if (outStats != nullptr)
            {
                *outStats = stats;
            }
            return;
        }
        // Malformed ranges never reach the optimizer: validate every submesh window and
        // every index BEFORE touching anything (a bad asset cooks as-is, loudly).
        for (usize i = 0; i < source.subStart.Size(); ++i)
        {
            const i64 start = source.subStart[i];
            const i64 count = (i < source.subCount.Size()) ? source.subCount[i] : -1;
            if (start < 0 || count < 0 ||
                start + count > static_cast<i64>(source.indexData.Size()))
            {
                LOG_WARNING(u8"Cook", u8"mesh '{}': submesh {} range invalid - skipping optimization",
                         source.name, i);
                if (outStats != nullptr)
                {
                    *outStats = stats;
                }
                return;
            }
        }
        for (const u32 index : source.indexData)
        {
            if (index >= vertexCount)
            {
                LOG_WARNING(u8"Cook",
                         u8"mesh '{}': index {} out of range ({} vertices) - skipping optimization",
                         source.name, index, vertexCount);
                if (outStats != nullptr)
                {
                    *outStats = stats;
                }
                return;
            }
        }

        stats.acmrBefore = TriangleAcmr(source, vertexCount);

        // 1+2. Per-triangle-submesh reorder. Positions sit at offset 0 of the 52-byte
        // vertex; 1.05 = meshoptimizer's recommended overdraw/cache balance.
        const auto* positions = reinterpret_cast<const float*>(source.vertexBlob.Data());
        for (usize i = 0; i < source.subStart.Size(); ++i)
        {
            const u8 prim = (i < source.subPrim.Size()) ? source.subPrim[i] : 0;
            const i32 count = source.subCount[i];
            if (static_cast<PrimitiveType>(prim) != PrimitiveType::Triangles || count < 3 ||
                (count % 3) != 0)
            {
                continue;
            }
            u32* range = source.indexData.Data() + source.subStart[i];
            meshopt_optimizeVertexCache(range, range, static_cast<usize>(count), vertexCount);
            meshopt_optimizeOverdraw(range, range, static_cast<usize>(count), positions,
                                     vertexCount, kStride, 1.05f);
            ++stats.triangleSubmeshes;
        }

        // 3. Whole-mesh vertex-fetch remap: blob reordered to index order, unused
        // vertices compacted away, every index rewritten through the remap.
        Array<u32> remap;
        remap.Resize(vertexCount);
        const usize uniqueCount = meshopt_optimizeVertexFetchRemap(
            remap.Data(), source.indexData.Data(), source.indexData.Size(), vertexCount);
        Array<u8> remappedBlob;
        remappedBlob.Resize(uniqueCount * kStride);
        meshopt_remapVertexBuffer(remappedBlob.Data(), source.vertexBlob.Data(), vertexCount,
                                  kStride, remap.Data());
        meshopt_remapIndexBuffer(source.indexData.Data(), source.indexData.Data(),
                                 source.indexData.Size(), remap.Data());
        source.vertexBlob = Move(remappedBlob);
        stats.verticesAfter = static_cast<u32>(uniqueCount);

        stats.acmrAfter = TriangleAcmr(source, uniqueCount);
        LOG_INFO(u8"Cook",
                 u8"mesh '{}': optimized {} triangle submesh(es), ACMR {} -> {}, "
                 u8"vertices {} -> {}",
                 source.name, stats.triangleSubmeshes, stats.acmrBefore, stats.acmrAfter,
                 stats.verticesBefore, stats.verticesAfter);
        if (outStats != nullptr)
        {
            *outStats = stats;
        }
    }
}
