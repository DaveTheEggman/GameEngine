/// Foundation::Terrain - `foundation.terrain`.
///
/// The terrain MODEL over a heightfield (foundation.heightfield): the chunk grid + quadtree the
/// chunked geo-mipmap renderer draws, per-chunk LOD selection given a camera, and the splat layer
/// descriptors. Pure data + math, NO RHI - every function here is unit-testable without a device.
/// A heightfield of side S = 64k+1 tiles into k x k chunks of 64 quads (65 verts) sharing edges.

module;
#include "Core/Prelude.h"

export module foundation.terrain;

import foundation.core;
import foundation.heightfield;
import foundation.lod; // the shared LOD selection math (coverage + threshold walk), one formula

using namespace foundation::core;
namespace lod = foundation::lod;

export namespace foundation::terrain
{
    /// One splat layer's PURE parameters (the material/texture refs live in foundation.terrain.
    /// resource). tileScale = how many times the layer texture repeats across the whole terrain.
    struct SplatLayer
    {
        f32 tileScale = 1.0f;
    };

    inline constexpr i32 kChunkQuads = 64; // quads per chunk side (65 verts, shared edges)
    inline constexpr i32 kChunkVerts = kChunkQuads + 1; // 65 verts per side
    inline constexpr u32 kMaxChunkLod = 6; // stride 2^6 = 64 -> a single quad (the coarsest)

    /// Chunks along one side of a heightfield of the given size (S = 64k+1 -> k).
    [[nodiscard]] constexpr i32 ChunksPerSide(i32 heightfieldSize) noexcept
    {
        return heightfieldSize > 1 ? (heightfieldSize - 1) / kChunkQuads : 0;
    }

    // ---- Shared chunk grid mesh (chunked geo-mipmapping) ----
    // ONE 65x65 vertex grid is uploaded once and drawn for EVERY chunk; per-chunk instance data
    // places it in the world and the VS fetches height from the terrain texture. Each LOD reuses the
    // SAME vertices through a stride-2^lod index buffer (finest = lod 0). RHI-free generation - the
    // renderer just uploads these arrays.

    /// The shared grid's vertex UVs: (u, v) in [0, 1] for the 65x65 grid, row-major (v-major). The
    /// renderer maps these to world XZ + the height sample coordinate per chunk.
    inline void BuildChunkGridVertices(Array<Float2>& out)
    {
        out.Clear();
        out.Reserve(static_cast<usize>(kChunkVerts) * kChunkVerts);
        const f32 span = static_cast<f32>(kChunkQuads);
        for (i32 z = 0; z < kChunkVerts; ++z)
        {
            for (i32 x = 0; x < kChunkVerts; ++x)
            {
                out.PushBack(Float2{static_cast<f32>(x) / span, static_cast<f32>(z) / span});
            }
        }
    }

    /// The 32-bit triangle indices for LOD `lod` (stride 2^lod) over the shared 65x65 grid: two
    /// triangles per quad, wound CCW when viewed from above (+Y). A lod past kMaxChunkLod yields none.
    inline void BuildChunkGridIndices(u32 lod, Array<u32>& out)
    {
        out.Clear();
        if (lod > kMaxChunkLod)
        {
            return;
        }
        const i32 stride = 1 << lod;
        const i32 quads = kChunkQuads / stride; // quads per side at this LOD
        out.Reserve(static_cast<usize>(quads) * quads * 6);
        for (i32 qz = 0; qz < quads; ++qz)
        {
            for (i32 qx = 0; qx < quads; ++qx)
            {
                const i32 x0 = qx * stride;
                const i32 z0 = qz * stride;
                const i32 x1 = x0 + stride;
                const i32 z1 = z0 + stride;
                const u32 v00 = static_cast<u32>(z0 * kChunkVerts + x0);
                const u32 v10 = static_cast<u32>(z0 * kChunkVerts + x1);
                const u32 v01 = static_cast<u32>(z1 * kChunkVerts + x0);
                const u32 v11 = static_cast<u32>(z1 * kChunkVerts + x1);
                out.PushBack(v00);
                out.PushBack(v01);
                out.PushBack(v11);
                out.PushBack(v00);
                out.PushBack(v11);
                out.PushBack(v10);
            }
        }
    }

    /// One terrain chunk: its position in the chunk grid, the grid origin of its sample block, and
    /// its LOCAL-space bounds (the renderer/physics apply the entity transform).
    struct TerrainChunk
    {
        i32 chunkX = 0;
        i32 chunkZ = 0;
        i32 gridX0 = 0; // sample-grid origin (covers [gridX0, gridX0+64] x [gridZ0, gridZ0+64])
        i32 gridZ0 = 0;
        AABB bounds = AABB::Empty();
    };

    /// Build the k x k chunk grid from a heightfield: each chunk's XZ footprint from the world
    /// mapping, its Y range from the heightfield's per-cell bounds. Row-major (chunkZ outer): the
    /// chunk at (cx, cz) is index cz * ChunksPerSide + cx.
    inline void BuildChunks(const heightfield::Heightfield& hf, Array<TerrainChunk>& out)
    {
        out.Clear();
        const i32 k = ChunksPerSide(hf.Size());
        for (i32 cz = 0; cz < k; ++cz)
        {
            for (i32 cx = 0; cx < k; ++cx)
            {
                const i32 gx0 = cx * kChunkQuads;
                const i32 gz0 = cz * kChunkQuads;
                f32 minY = 0.0f;
                f32 maxY = 0.0f;
                hf.CellBounds(gx0, gz0, gx0 + kChunkQuads, gz0 + kChunkQuads, minY, maxY);
                const Float2 w0 = hf.GridToWorld(static_cast<f32>(gx0), static_cast<f32>(gz0));
                const Float2 w1 = hf.GridToWorld(static_cast<f32>(gx0 + kChunkQuads),
                                                 static_cast<f32>(gz0 + kChunkQuads));
                TerrainChunk chunk;
                chunk.chunkX = cx;
                chunk.chunkZ = cz;
                chunk.gridX0 = gx0;
                chunk.gridZ0 = gz0;
                chunk.bounds.min = Float3{w0.x, minY, w0.y};
                chunk.bounds.max = Float3{w1.x, maxY, w1.y};
                out.PushBack(chunk);
            }
        }
    }

    /// A chunk's LOCAL-space bounding sphere, the bridge to the shared LOD coverage metric.
    [[nodiscard]] inline BoundingSphere ChunkBoundingSphere(const TerrainChunk& chunk) noexcept
    {
        BoundingSphere sphere;
        sphere.center = chunk.bounds.Center();
        sphere.radius = Length(chunk.bounds.Extents());
        return sphere;
    }

    /// Per-chunk LOD via the SHARED coverage metric (foundation.lod - ONE formula for meshes AND
    /// terrain). The chunk's local bounding sphere is placed in the world by `chunkToWorld`
    /// (translation + Y-rotation per the terrain instance model, so the radius is preserved),
    /// projected to screen coverage, then walked against the DESCENDING `coverageThresholds`
    /// (thresholds[0] = 1.0 by convention, level 0 = fallback). Pure - unit-testable headless.
    [[nodiscard]] inline u32 SelectChunkLod(const TerrainChunk& chunk, const Float4x4& chunkToWorld,
                                            const Float4x4& view, const Float4x4& projection,
                                            Span<const f32> coverageThresholds,
                                            f32 bias = 0.0f) noexcept
    {
        const BoundingSphere sphere = ChunkBoundingSphere(chunk);
        const Float3 worldCenter = TransformPoint(sphere.center, chunkToWorld);
        const f32 coverage =
            lod::ProjectedSphereCoverage(view, projection, worldCenter, sphere.radius, bias);
        return lod::SelectLevelByCoverage(coverageThresholds, coverage);
    }

    /// Per-chunk LOD levels for a view (the renderer feeds these to the draw).
    inline void SelectChunkLods(Span<const TerrainChunk> chunks, const Float4x4& chunkToWorld,
                                const Float4x4& view, const Float4x4& projection,
                                Span<const f32> coverageThresholds, f32 bias, Array<u32>& out)
    {
        out.Clear();
        out.Reserve(chunks.Size());
        for (usize i = 0; i < chunks.Size(); ++i)
        {
            out.PushBack(
                SelectChunkLod(chunks[i], chunkToWorld, view, projection, coverageThresholds, bias));
        }
    }

    /// A quadtree over the chunk grid for hierarchical frustum culling. Leaves are single chunks;
    /// internal nodes carry the union bounds so a Disjoint node prunes its whole subtree. Works for
    /// any k (uneven splits when a side is odd).
    class TerrainQuadtree
    {
    public:
        TerrainQuadtree() = default;

        void Build(Span<const TerrainChunk> chunks, i32 chunksPerSide)
        {
            m_nodes.Clear();
            m_chunks = chunks;
            m_side = chunksPerSide;
            if (chunksPerSide > 0 && chunks.Size() >= static_cast<usize>(chunksPerSide) * chunksPerSide)
            {
                BuildNode(0, chunksPerSide, 0, chunksPerSide);
            }
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_nodes.IsEmpty(); }
        [[nodiscard]] usize NodeCount() const noexcept { return m_nodes.Size(); }

        /// Gather the indices of chunks whose bounds are not disjoint from the frustum.
        void Cull(const BoundingFrustum& frustum, Array<i32>& outVisible) const
        {
            outVisible.Clear();
            if (!m_nodes.IsEmpty())
            {
                CullNode(0, frustum, outVisible);
            }
        }

    private:
        struct Node
        {
            AABB bounds = AABB::Empty();
            i32 chunkIndex = -1; // >= 0 = leaf
            i32 childCount = 0;
            i32 children[4] = {-1, -1, -1, -1};
        };

        // Build over the chunk-index rectangle [x0, x1) x [z0, z1); returns the node index.
        i32 BuildNode(i32 x0, i32 x1, i32 z0, i32 z1)
        {
            const i32 nodeIdx = static_cast<i32>(m_nodes.Size());
            m_nodes.PushBack(Node{});
            if (x1 - x0 == 1 && z1 - z0 == 1)
            {
                const i32 ci = z0 * m_side + x0;
                m_nodes[static_cast<usize>(nodeIdx)].chunkIndex = ci;
                m_nodes[static_cast<usize>(nodeIdx)].bounds = m_chunks[static_cast<usize>(ci)].bounds;
                return nodeIdx;
            }
            const i32 xm = (x1 - x0 > 1) ? (x0 + x1) / 2 : x1;
            const i32 zm = (z1 - z0 > 1) ? (z0 + z1) / 2 : z1;
            const i32 xEdges[3] = {x0, xm, x1};
            const i32 zEdges[3] = {z0, zm, z1};
            const i32 xCount = (x1 - x0 > 1) ? 2 : 1;
            const i32 zCount = (z1 - z0 > 1) ? 2 : 1;
            AABB bounds = AABB::Empty();
            i32 childCount = 0;
            for (i32 zi = 0; zi < zCount; ++zi)
            {
                for (i32 xi = 0; xi < xCount; ++xi)
                {
                    const i32 child =
                        BuildNode(xEdges[xi], xEdges[xi + 1], zEdges[zi], zEdges[zi + 1]);
                    m_nodes[static_cast<usize>(nodeIdx)].children[childCount++] = child;
                    bounds = Merge(bounds, m_nodes[static_cast<usize>(child)].bounds);
                }
            }
            m_nodes[static_cast<usize>(nodeIdx)].childCount = childCount;
            m_nodes[static_cast<usize>(nodeIdx)].bounds = bounds;
            return nodeIdx;
        }

        void CullNode(i32 nodeIdx, const BoundingFrustum& frustum, Array<i32>& out) const
        {
            const Node& node = m_nodes[static_cast<usize>(nodeIdx)];
            if (Contains(frustum, node.bounds) == ContainmentType::Disjoint)
            {
                return;
            }
            if (node.chunkIndex >= 0)
            {
                out.PushBack(node.chunkIndex);
                return;
            }
            for (i32 i = 0; i < node.childCount; ++i)
            {
                CullNode(node.children[i], frustum, out);
            }
        }

        Array<Node> m_nodes;
        Span<const TerrainChunk> m_chunks;
        i32 m_side = 0;
    };
}
