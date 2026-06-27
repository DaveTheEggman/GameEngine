// Slice 2 (mesh upload) — the mesh GPU cache uploads a StaticMesh's vertex/index
// streams to RHI buffers on first use and reuses them after. Exercised on the Null RHI.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rhi.null;
import raptor.geometry;
import raptor.render;

using namespace raptor::core;
using namespace raptor::render;
namespace rhi = raptor::rhi;
namespace geo = raptor::geometry;

TEST_CASE("mesh GPU cache: uploads on first use, reuses after, frees on clear")
{
    rhi::null::NullDevice device;
    GpuMeshCache cache(device);

    RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(1.0f);

    const GpuMesh* g = cache.GetOrUpload(cube.Get());
    REQUIRE(g != nullptr);
    CHECK(g->vertexBuffer != nullptr);
    CHECK(g->indexBuffer != nullptr);
    CHECK(g->indexCount == cube->IndexCount());          // 36
    CHECK(g->indexFormat == rhi::IndexFormat::UInt32);
    CHECK(cache.Size() == 1);

    // second request for the same mesh returns the same cached entry (no re-upload)
    const GpuMesh* again = cache.GetOrUpload(cube.Get());
    CHECK(again == g);
    CHECK(cache.Size() == 1);

    // a different mesh is a distinct entry
    RefPtr<geo::StaticMesh> sphere = geo::Primitives::Sphere(1.0f, 8, 4);
    const GpuMesh* s = cache.GetOrUpload(sphere.Get());
    REQUIRE(s != nullptr);
    CHECK(s->indexCount == sphere->IndexCount());
    CHECK(cache.Size() == 2);

    cache.Clear();
    CHECK(cache.Size() == 0);
}

TEST_CASE("mesh GPU cache: null + empty meshes upload nothing")
{
    rhi::null::NullDevice device;
    GpuMeshCache cache(device);
    CHECK(cache.GetOrUpload(nullptr) == nullptr);
    geo::StaticMesh empty;
    CHECK(cache.GetOrUpload(&empty) == nullptr);          // no vertices/indices
    CHECK(cache.Size() == 0);
}
