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

using namespace foundation::core;

export namespace foundation::terrain
{
    /// One splat layer's PURE parameters (the material/texture refs live in foundation.terrain.
    /// resource). tileScale = how many times the layer texture repeats across the whole terrain.
    struct SplatLayer
    {
        f32 tileScale = 1.0f;
    };

    inline constexpr i32 kChunkQuads = 64; // quads per chunk side (65 verts, shared edges)

    /// Chunks along one side of a heightfield of the given size (S = 64k+1 -> k).
    [[nodiscard]] constexpr i32 ChunksPerSide(i32 heightfieldSize) noexcept
    {
        return heightfieldSize > 1 ? (heightfieldSize - 1) / kChunkQuads : 0;
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

    /// Distance from a point to the closest point on a chunk's bounds (0 inside). LOD input.
    [[nodiscard]] inline f32 DistanceToChunk(const TerrainChunk& chunk, Float3 point) noexcept
    {
        const auto clamp = [](f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); };
        const Float3 c{clamp(point.x, chunk.bounds.min.x, chunk.bounds.max.x),
                       clamp(point.y, chunk.bounds.min.y, chunk.bounds.max.y),
                       clamp(point.z, chunk.bounds.min.z, chunk.bounds.max.z)};
        return Length(point - c);
    }

    /// The LOD level for a distance: `lodDistances[i]` is the FAR edge of LOD i (0 = finest). A
    /// distance beyond the last entry returns the coarsest level (= lodDistances.Size()). This is the
    /// RHI-FREE fallback metric (headless/tests); the RENDERER prefers coverage - see
    /// SelectLodByCoverage + ChunkBoundingSphere, fed by foundation.render's LodCoverageFor
    /// (fov/resolution-aware, shared with mesh-lod - not a second formula).
    [[nodiscard]] inline u32 SelectLod(f32 distance, Span<const f32> lodDistances) noexcept
    {
        for (u32 i = 0; i < static_cast<u32>(lodDistances.Size()); ++i)
        {
            if (distance <= lodDistances[i])
            {
                return i;
            }
        }
        return static_cast<u32>(lodDistances.Size());
    }

    /// A chunk's LOCAL-space bounding sphere, to feed the renderer's coverage metric
    /// (foundation.render's LodCoverageFor, after the entity transform is applied to the centre).
    [[nodiscard]] inline BoundingSphere ChunkBoundingSphere(const TerrainChunk& chunk) noexcept
    {
        BoundingSphere sphere;
        sphere.center = chunk.bounds.Center();
        sphere.radius = Length(chunk.bounds.Extents());
        return sphere;
    }

    /// The LOD level for a screen COVERAGE (the renderer's preferred metric, from LodCoverageFor):
    /// `coverageThresholds[i]` is the MINIMUM coverage for LOD i, DESCENDING (0 = finest). Coverage
    /// below the last threshold returns the coarsest level (= size()). Pure - the same threshold-walk
    /// shape as mesh-lod's PickLodLevel, generalized to a Span so terrain shares the selection logic.
    [[nodiscard]] inline u32 SelectLodByCoverage(f32 coverage,
                                                 Span<const f32> coverageThresholds) noexcept
    {
        for (u32 i = 0; i < static_cast<u32>(coverageThresholds.Size()); ++i)
        {
            if (coverage >= coverageThresholds[i])
            {
                return i;
            }
        }
        return static_cast<u32>(coverageThresholds.Size());
    }

    /// Per-chunk LOD levels for a camera position (pure - the renderer feeds these to the draw).
    inline void SelectChunkLods(Span<const TerrainChunk> chunks, Float3 cameraPos,
                                Span<const f32> lodDistances, Array<u32>& out)
    {
        out.Clear();
        out.Reserve(chunks.Size());
        for (usize i = 0; i < chunks.Size(); ++i)
        {
            out.PushBack(SelectLod(DistanceToChunk(chunks[i], cameraPos), lodDistances));
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
