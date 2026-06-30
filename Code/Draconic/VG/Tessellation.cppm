// Draconic::VG — :tessellation partition.
//
// Converts paths/polylines into triangle meshes:
//   * Triangulator    — ear-clipping fill triangulation (+ holes), winding helpers
//   * FillTessellator — filled paths, with an analytical-AA fringe ring
//   * StrokeTessellator — stroked polylines with joins, caps, dashing, AA fringe
// Ported from Sedulous.VG (Triangulator/FillTessellator/StrokeTessellator). The
// colors are the engine's float Color throughout; packing to the vertex's
// Color32 happens only in the VGVertex constructor.

module;
#include "Core/Prelude.h"

export module draconic.vg:tessellation;

import draconic.core;
import :enums;
import :vertex;
import :style;
import :fills;
import :path;
import :shapes;

using namespace draconic::core;

export namespace draconic::vg
{
    /// Triangulates polygons using the ear-clipping algorithm.
    class Triangulator
    {
    public:
        /// Signed area of a polygon. Positive = CCW, negative = CW.
        [[nodiscard]] static f32 PolygonArea(Span<const Vec2> contour)
        {
            f32 area = 0.0f;
            const i32 n = static_cast<i32>(contour.Size());
            for (i32 i = 0; i < n; ++i)
            {
                const i32 j = (i + 1) % n;
                area += contour[i].x * contour[j].y;
                area -= contour[j].x * contour[i].y;
            }
            return area * 0.5f;
        }

        /// Check if a point is inside a triangle using barycentric sign tests.
        [[nodiscard]] static bool PointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
        {
            const f32 d1 = Sign(p, a, b);
            const f32 d2 = Sign(p, b, c);
            const f32 d3 = Sign(p, c, a);

            const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
            const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
            return !(hasNeg && hasPos);
        }

        /// Whether the angle at p1 is convex given CCW winding.
        [[nodiscard]] static bool IsConvex(Vec2 p0, Vec2 p1, Vec2 p2)
        {
            return Cross(p1 - p0, p2 - p0) > 0.0f;
        }

        /// Triangulate a simple polygon using ear-clipping. Indices are appended
        /// using baseIndex as the vertex offset.
        static void Triangulate(Span<const Vec2> contour, FillRule /*fillRule*/, Array<u32>& indices, u32 baseIndex = 0)
        {
            const i32 n = static_cast<i32>(contour.Size());
            if (n < 3)
                return;

            // Convex polygons: simple fan (fast path).
            if (IsConvexPolygon(contour))
            {
                const f32 area = PolygonArea(contour);
                if (area > 0.0f) // CCW
                {
                    for (i32 i = 1; i < n - 1; ++i)
                    {
                        indices.PushBack(baseIndex);
                        indices.PushBack(baseIndex + static_cast<u32>(i));
                        indices.PushBack(baseIndex + static_cast<u32>(i + 1));
                    }
                }
                else // CW - reverse winding
                {
                    for (i32 i = 1; i < n - 1; ++i)
                    {
                        indices.PushBack(baseIndex);
                        indices.PushBack(baseIndex + static_cast<u32>(i + 1));
                        indices.PushBack(baseIndex + static_cast<u32>(i));
                    }
                }
                return;
            }

            // Ear-clipping for concave polygons. Build a mutable index list.
            Array<i32> idxList;
            const f32 area = PolygonArea(contour);
            if (area > 0.0f) // CCW - keep order
            {
                for (i32 i = 0; i < n; ++i)
                    idxList.PushBack(i);
            }
            else // CW - reverse to CCW
            {
                for (i32 i = n - 1; i >= 0; --i)
                    idxList.PushBack(i);
            }

            i32 failCount = 0;
            i32 i = 0;

            while (idxList.Size() > 2)
            {
                const i32 listCount = static_cast<i32>(idxList.Size());
                if (failCount >= listCount)
                {
                    // Degenerate - fallback to fan triangulation for remaining vertices.
                    for (i32 k = 1; k < listCount - 1; ++k)
                    {
                        indices.PushBack(baseIndex + static_cast<u32>(idxList[0]));
                        indices.PushBack(baseIndex + static_cast<u32>(idxList[static_cast<usize>(k)]));
                        indices.PushBack(baseIndex + static_cast<u32>(idxList[static_cast<usize>(k + 1)]));
                    }
                    break;
                }

                const i32 count = listCount;
                const i32 iPrev = (i + count - 1) % count;
                const i32 iCurr = i % count;
                const i32 iNext = (i + 1) % count;

                const i32 vPrev = idxList[static_cast<usize>(iPrev)];
                const i32 vCurr = idxList[static_cast<usize>(iCurr)];
                const i32 vNext = idxList[static_cast<usize>(iNext)];

                const Vec2 pPrev = contour[static_cast<usize>(vPrev)];
                const Vec2 pCurr = contour[static_cast<usize>(vCurr)];
                const Vec2 pNext = contour[static_cast<usize>(vNext)];

                if (IsEarTip(pPrev, pCurr, pNext, contour, idxList, iCurr))
                {
                    indices.PushBack(baseIndex + static_cast<u32>(vPrev));
                    indices.PushBack(baseIndex + static_cast<u32>(vCurr));
                    indices.PushBack(baseIndex + static_cast<u32>(vNext));

                    idxList.RemoveAt(static_cast<usize>(iCurr));
                    failCount = 0;

                    // Stay at same index (next vertex shifted into current position).
                    if (i >= static_cast<i32>(idxList.Size()))
                        i = 0;
                }
                else
                {
                    i = (i + 1) % static_cast<i32>(idxList.Size());
                    ++failCount;
                }
            }
        }

        /// Triangulate a polygon with holes (bridges each hole into the outer contour).
        static void TriangulateWithHoles(Span<const Vec2> outer, Span<const Span<const Vec2>> holes, FillRule fillRule,
                                         Array<u32>& indices, Array<Vec2>& mergedVertices)
        {
            if (outer.Size() < 3)
                return;

            if (holes.Size() == 0)
            {
                const u32 baseIndex = static_cast<u32>(mergedVertices.Size());
                for (usize p = 0; p < outer.Size(); ++p)
                    mergedVertices.PushBack(outer[p]);
                Triangulate(Span<const Vec2>(mergedVertices.Data() + baseIndex, outer.Size()), fillRule, indices, baseIndex);
                return;
            }

            Array<Vec2> merged;
            for (usize p = 0; p < outer.Size(); ++p)
                merged.PushBack(outer[p]);

            // Sort holes by descending max-X (insertion sort over the small index list).
            Array<i32> sortedHoles;
            for (usize i = 0; i < holes.Size(); ++i)
                sortedHoles.PushBack(static_cast<i32>(i));
            for (usize a = 1; a < sortedHoles.Size(); ++a)
            {
                const i32 key = sortedHoles[a];
                const f32 keyMaxX = MaxX(holes[static_cast<usize>(key)]);
                usize b = a;
                while (b > 0 && MaxX(holes[static_cast<usize>(sortedHoles[b - 1])]) < keyMaxX)
                {
                    sortedHoles[b] = sortedHoles[b - 1];
                    --b;
                }
                sortedHoles[b] = key;
            }

            for (usize s = 0; s < sortedHoles.Size(); ++s)
                MergeHole(merged, holes[static_cast<usize>(sortedHoles[s])]);

            const u32 baseIndex = static_cast<u32>(mergedVertices.Size());
            for (usize p = 0; p < merged.Size(); ++p)
                mergedVertices.PushBack(merged[p]);
            Triangulate(Span<const Vec2>(mergedVertices.Data() + baseIndex, merged.Size()), fillRule, indices, baseIndex);
        }

    private:
        [[nodiscard]] static f32 Sign(Vec2 p1, Vec2 p2, Vec2 p3)
        {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        }

        [[nodiscard]] static f32 Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

        [[nodiscard]] static f32 MaxX(Span<const Vec2> pts)
        {
            f32 m = -3.4028235e38f;
            for (usize i = 0; i < pts.Size(); ++i)
                if (pts[i].x > m) m = pts[i].x;
            return m;
        }

        [[nodiscard]] static bool IsConvexPolygon(Span<const Vec2> contour)
        {
            const i32 n = static_cast<i32>(contour.Size());
            if (n < 3) return false;

            bool gotPositive = false;
            bool gotNegative = false;

            for (i32 i = 0; i < n; ++i)
            {
                const Vec2 p0 = contour[static_cast<usize>(i)];
                const Vec2 p1 = contour[static_cast<usize>((i + 1) % n)];
                const Vec2 p2 = contour[static_cast<usize>((i + 2) % n)];
                const f32 cross = Cross(p1 - p0, p2 - p1);

                if (cross > 0.0001f) gotPositive = true;
                if (cross < -0.0001f) gotNegative = true;
                if (gotPositive && gotNegative)
                    return false;
            }
            return true;
        }

        [[nodiscard]] static bool IsEarTip(Vec2 pPrev, Vec2 pCurr, Vec2 pNext, Span<const Vec2> contour,
                                           const Array<i32>& idxList, i32 currIdx)
        {
            // Must be convex (CCW winding).
            if (Cross(pCurr - pPrev, pNext - pPrev) <= 0.0f)
                return false;

            const i32 count = static_cast<i32>(idxList.Size());
            for (i32 i = 0; i < count; ++i)
            {
                if (i == currIdx)
                    continue;
                if (i == (currIdx + count - 1) % count)
                    continue;
                if (i == (currIdx + 1) % count)
                    continue;

                const Vec2 p = contour[static_cast<usize>(idxList[static_cast<usize>(i)])];

                // Skip if the point is one of the triangle vertices (duplicate points).
                if ((p.x == pPrev.x && p.y == pPrev.y) || (p.x == pCurr.x && p.y == pCurr.y) || (p.x == pNext.x && p.y == pNext.y))
                    continue;

                if (PointInTriangle(p, pPrev, pCurr, pNext))
                    return false;
            }
            return true;
        }

        static void MergeHole(Array<Vec2>& outer, Span<const Vec2> hole)
        {
            if (hole.Size() == 0)
                return;

            usize rightmostIdx = 0;
            for (usize i = 1; i < hole.Size(); ++i)
                if (hole[i].x > hole[rightmostIdx].x)
                    rightmostIdx = i;

            const Vec2 holePoint = hole[rightmostIdx];

            usize bestOuterIdx = 0;
            f32 bestDist = 3.4028235e38f;
            for (usize i = 0; i < outer.Size(); ++i)
            {
                const f32 dist = DistanceSquared(holePoint, outer[i]);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestOuterIdx = i;
                }
            }

            const usize insertPos = bestOuterIdx + 1;
            const Vec2 bridgePoint = outer[bestOuterIdx];

            // Build the bridged hole vertex run: hole (from rightmost, wrapping) + closing
            // hole point + bridge back to the outer vertex.
            Array<Vec2> holeVerts;
            for (usize i = 0; i <= hole.Size(); ++i)
                holeVerts.PushBack(hole[(rightmostIdx + i) % hole.Size()]);
            holeVerts.PushBack(bridgePoint);

            // Insert holeVerts into outer at insertPos (rebuild — Array has no range insert).
            Array<Vec2> rebuilt;
            for (usize i = 0; i < insertPos; ++i)
                rebuilt.PushBack(outer[i]);
            for (usize i = 0; i < holeVerts.Size(); ++i)
                rebuilt.PushBack(holeVerts[i]);
            for (usize i = insertPos; i < outer.Size(); ++i)
                rebuilt.PushBack(outer[i]);
            outer = Move(rebuilt);
        }
    };

    /// Tessellates filled paths into triangle meshes.
    class FillTessellator
    {
    public:
        /// Tessellate a filled path into vertices and indices.
        static void Tessellate(const Path& path, FillRule fillRule, Color color, bool antiAlias,
                               Array<VGVertex>& vertices, Array<u32>& indices, f32 tolerance = 0.25f)
        {
            Array<FlattenedSubPath> subPaths;
            PathFlattener::Flatten(path, tolerance, subPaths);
            if (subPaths.IsEmpty())
                return;

            for (usize s = 0; s < subPaths.Size(); ++s)
            {
                const FlattenedSubPath& subPath = subPaths[s];
                if (subPath.points.Size() < 3)
                    continue;

                usize pointCount = subPath.points.Size();
                if (pointCount > 1 && Distance(subPath.points[0], subPath.points[pointCount - 1]) < 0.0001f)
                    --pointCount;
                if (pointCount < 3)
                    continue;

                const Span<const Vec2> points(subPath.points.Data(), pointCount);

                if (antiAlias)
                {
                    TessellateWithAA(points, fillRule, color, vertices, indices);
                }
                else
                {
                    const u32 baseIndex = static_cast<u32>(vertices.Size());
                    for (usize i = 0; i < pointCount; ++i)
                        vertices.PushBack(VGVertex::Solid(points[i], color));
                    Triangulator::Triangulate(points, fillRule, indices, baseIndex);
                }
            }
        }

        /// Tessellate a filled path with an IVGFill style.
        static void TessellateWithFill(const Path& path, FillRule fillRule, const IVGFill& fill, bool antiAlias,
                                       Array<VGVertex>& vertices, Array<u32>& indices, f32 tolerance = 0.25f)
        {
            if (!fill.RequiresInterpolation())
            {
                Tessellate(path, fillRule, fill.BaseColor(), antiAlias, vertices, indices, tolerance);
                return;
            }

            const Rect bounds = path.GetBounds();

            Array<FlattenedSubPath> subPaths;
            PathFlattener::Flatten(path, tolerance, subPaths);
            if (subPaths.IsEmpty())
                return;

            for (usize s = 0; s < subPaths.Size(); ++s)
            {
                const FlattenedSubPath& subPath = subPaths[s];
                if (subPath.points.Size() < 3)
                    continue;

                usize pointCount = subPath.points.Size();
                if (pointCount > 1 && Distance(subPath.points[0], subPath.points[pointCount - 1]) < 0.0001f)
                    --pointCount;
                if (pointCount < 3)
                    continue;

                const Span<const Vec2> points(subPath.points.Data(), pointCount);

                if (antiAlias)
                {
                    TessellateWithAAFill(points, fillRule, fill, bounds, vertices, indices);
                }
                else
                {
                    const u32 baseIndex = static_cast<u32>(vertices.Size());
                    for (usize i = 0; i < pointCount; ++i)
                        vertices.PushBack(VGVertex::Solid(points[i], fill.GetColorAt(points[i], bounds)));
                    Triangulator::Triangulate(points, fillRule, indices, baseIndex);
                }
            }
        }

    private:
        static constexpr f32 FringeWidth = 0.75f;

        /// Compute per-vertex averaged outward normals (miter-like, clamped).
        static void ComputeFringeNormals(Span<const Vec2> points, Array<Vec2>& normals)
        {
            const i32 n = static_cast<i32>(points.Size());
            normals.Resize(static_cast<usize>(n));

            const f32 area = Triangulator::PolygonArea(points);
            const f32 sign = (area > 0.0f) ? 1.0f : -1.0f; // CCW = positive area

            for (i32 i = 0; i < n; ++i)
            {
                const i32 prev = (i + n - 1) % n;
                const i32 next = (i + 1) % n;

                Vec2 e0 = points[static_cast<usize>(i)] - points[static_cast<usize>(prev)];
                Vec2 e1 = points[static_cast<usize>(next)] - points[static_cast<usize>(i)];
                const f32 len0 = Length(e0);
                const f32 len1 = Length(e1);
                if (len0 > 0.0001f) e0 = e0 / len0;
                if (len1 > 0.0001f) e1 = e1 / len1;

                // Outward normals (perpendicular to edge, direction based on winding).
                const Vec2 n0 = Vec2{ e0.y, -e0.x } * sign;
                const Vec2 n1 = Vec2{ e1.y, -e1.x } * sign;

                Vec2 avg = (n0 + n1) * 0.5f;
                const f32 avgLen = Length(avg);
                if (avgLen > 0.0001f)
                {
                    avg = avg / avgLen;
                    // Scale to maintain consistent fringe width at corners (miter-like),
                    // clamped to prevent extreme normals at sharp concave angles.
                    const f32 dot = n0.x * avg.x + n0.y * avg.y;
                    if (dot > 0.1f)
                        avg = avg * Min(1.0f / dot, 3.0f);
                }
                else
                {
                    avg = n0;
                }
                normals[static_cast<usize>(i)] = avg;
            }
        }

        /// Emit the inner-fill triangulation + the inner/outer fringe quad strip.
        static void EmitFringeRing(Span<const Vec2> points, FillRule fillRule, const Array<Vec2>& normals,
                                   const Array<Color>& innerColors, const Array<Color>& outerColors,
                                   Array<VGVertex>& vertices, Array<u32>& indices)
        {
            const i32 n = static_cast<i32>(points.Size());

            const u32 innerBaseIdx = static_cast<u32>(vertices.Size());
            Array<Vec2> innerPoints;
            innerPoints.Resize(static_cast<usize>(n));
            for (i32 i = 0; i < n; ++i)
            {
                innerPoints[static_cast<usize>(i)] = points[static_cast<usize>(i)] - normals[static_cast<usize>(i)] * (FringeWidth * 0.5f);
                vertices.PushBack(VGVertex::Solid(innerPoints[static_cast<usize>(i)], innerColors[static_cast<usize>(i)], 1.0f));
            }

            Triangulator::Triangulate(Span<const Vec2>(innerPoints.Data(), static_cast<usize>(n)), fillRule, indices, innerBaseIdx);

            const u32 outerBaseIdx = static_cast<u32>(vertices.Size());
            for (i32 i = 0; i < n; ++i)
            {
                const Vec2 outerPt = points[static_cast<usize>(i)] + normals[static_cast<usize>(i)] * (FringeWidth * 0.5f);
                vertices.PushBack(VGVertex::Solid(outerPt, outerColors[static_cast<usize>(i)], 0.0f));
            }

            for (i32 i = 0; i < n; ++i)
            {
                const u32 j = static_cast<u32>((i + 1) % n);
                const u32 ui = static_cast<u32>(i);
                const u32 i0 = innerBaseIdx + ui;
                const u32 i1 = innerBaseIdx + j;
                const u32 o0 = outerBaseIdx + ui;
                const u32 o1 = outerBaseIdx + j;

                indices.PushBack(i0); indices.PushBack(i1); indices.PushBack(o1);
                indices.PushBack(i0); indices.PushBack(o1); indices.PushBack(o0);
            }
        }

        static void TessellateWithAA(Span<const Vec2> points, FillRule fillRule, Color color,
                                     Array<VGVertex>& vertices, Array<u32>& indices)
        {
            const usize n = points.Size();
            Array<Vec2> normals;
            ComputeFringeNormals(points, normals);

            const Color transColor{ color.r, color.g, color.b, 0.0f };
            Array<Color> innerColors;
            Array<Color> outerColors;
            innerColors.Resize(n);
            outerColors.Resize(n);
            for (usize i = 0; i < n; ++i) { innerColors[i] = color; outerColors[i] = transColor; }

            EmitFringeRing(points, fillRule, normals, innerColors, outerColors, vertices, indices);
        }

        static void TessellateWithAAFill(Span<const Vec2> points, FillRule fillRule, const IVGFill& fill, Rect bounds,
                                         Array<VGVertex>& vertices, Array<u32>& indices)
        {
            const usize n = points.Size();
            Array<Vec2> normals;
            ComputeFringeNormals(points, normals);

            Array<Color> innerColors;
            Array<Color> outerColors;
            innerColors.Resize(n);
            outerColors.Resize(n);
            for (usize i = 0; i < n; ++i)
            {
                const Color fc = fill.GetColorAt(points[i], bounds);
                innerColors[i] = fc;
                outerColors[i] = Color{ fc.r, fc.g, fc.b, 0.0f };
            }

            EmitFringeRing(points, fillRule, normals, innerColors, outerColors, vertices, indices);
        }
    };

    /// Tessellates stroked polylines into triangle meshes with joins and caps.
    class StrokeTessellator
    {
    public:
        /// Tessellate a stroked polyline.
        static void Tessellate(Span<const Vec2> points, bool closed, StrokeStyle style, Span<const f32> dashPattern,
                               bool antiAlias, Color color, Array<VGVertex>& vertices, Array<u32>& indices)
        {
            if (points.Size() < 2)
                return;

            // Apply dashing if a pattern is provided.
            if (dashPattern.Size() >= 2)
            {
                Array<Array<Vec2>> dashSegments;
                DashGenerator::GenerateDashes(points, closed, dashPattern, style.dashOffset, dashSegments);
                for (usize d = 0; d < dashSegments.Size(); ++d)
                {
                    const Array<Vec2>& seg = dashSegments[d];
                    if (seg.Size() >= 2)
                        TessellateSegment(Span<const Vec2>(seg.Data(), seg.Size()), false, style, antiAlias, color, vertices, indices);
                }
                return;
            }

            TessellateSegment(points, closed, style, antiAlias, color, vertices, indices);
        }

    private:
        static void TessellateSegment(Span<const Vec2> points, bool closed, StrokeStyle style,
                                      bool antiAlias, Color color, Array<VGVertex>& vertices, Array<u32>& indices)
        {
            const i32 n = static_cast<i32>(points.Size());
            if (n < 2)
                return;

            const f32 fringeWidth = antiAlias ? 0.75f : 0.0f;
            const f32 halfWidth = style.width * 0.5f;

            // Pre-compute edge directions, normals, and lengths.
            const i32 edgeCount = closed ? n : n - 1;
            Array<Vec2> edgeDirs; edgeDirs.Resize(static_cast<usize>(edgeCount));
            Array<Vec2> edgeNormals; edgeNormals.Resize(static_cast<usize>(edgeCount));
            Array<f32> edgeLens; edgeLens.Resize(static_cast<usize>(edgeCount));
            for (i32 i = 0; i < edgeCount; ++i)
            {
                const Vec2 p0 = points[static_cast<usize>(i)];
                const Vec2 p1 = points[static_cast<usize>((i + 1) % n)];
                Vec2 dir = p1 - p0;
                const f32 len = Length(dir);
                edgeLens[static_cast<usize>(i)] = len;
                if (len > 0.0001f)
                {
                    dir = dir / len;
                    edgeDirs[static_cast<usize>(i)] = dir;
                    edgeNormals[static_cast<usize>(i)] = Vec2{ -dir.y, dir.x };
                }
                else
                {
                    edgeDirs[static_cast<usize>(i)] = Vec2{ 0.0f, 0.0f };
                    edgeNormals[static_cast<usize>(i)] = Vec2{ 0.0f, 1.0f };
                }
            }

            // Per-vertex miter-scaled normals + unit normals (for fringe).
            Array<Vec2> vertNormals; vertNormals.Resize(static_cast<usize>(n));
            Array<Vec2> unitNormals; unitNormals.Resize(static_cast<usize>(n));
            for (i32 i = 0; i < n; ++i)
            {
                if (closed)
                {
                    const i32 prevEdge = (i + edgeCount - 1) % edgeCount;
                    const i32 nextEdge = i % edgeCount;
                    const f32 minLen = Min(edgeLens[static_cast<usize>(prevEdge)], edgeLens[static_cast<usize>(nextEdge)]);
                    ComputeJoinNormal(edgeNormals[static_cast<usize>(prevEdge)], edgeNormals[static_cast<usize>(nextEdge)], style, halfWidth, minLen,
                                      vertNormals[static_cast<usize>(i)], unitNormals[static_cast<usize>(i)]);
                }
                else if (i == 0)
                {
                    vertNormals[0] = edgeNormals[0];
                    unitNormals[0] = edgeNormals[0];
                }
                else if (i == n - 1)
                {
                    vertNormals[static_cast<usize>(i)] = edgeNormals[static_cast<usize>(edgeCount - 1)];
                    unitNormals[static_cast<usize>(i)] = edgeNormals[static_cast<usize>(edgeCount - 1)];
                }
                else
                {
                    const f32 minLen = Min(edgeLens[static_cast<usize>(i - 1)], edgeLens[static_cast<usize>(i)]);
                    ComputeJoinNormal(edgeNormals[static_cast<usize>(i - 1)], edgeNormals[static_cast<usize>(i)], style, halfWidth, minLen,
                                      vertNormals[static_cast<usize>(i)], unitNormals[static_cast<usize>(i)]);
                }
            }

            // Determine which vertices need bevel/round joins.
            Array<bool> needsJoin; needsJoin.Resize(static_cast<usize>(n));
            if (style.join != VGLineJoin::Miter)
            {
                for (i32 i = 0; i < n; ++i)
                {
                    const bool isJoinVertex = closed || (i > 0 && i < n - 1);
                    if (!isJoinVertex) continue;

                    const i32 prevEdge = closed ? (i + edgeCount - 1) % edgeCount : i - 1;
                    const i32 nextEdge = closed ? i % edgeCount : i;
                    if (prevEdge < 0 || nextEdge >= edgeCount) continue;

                    const f32 cross = edgeNormals[static_cast<usize>(prevEdge)].x * edgeNormals[static_cast<usize>(nextEdge)].y -
                                      edgeNormals[static_cast<usize>(prevEdge)].y * edgeNormals[static_cast<usize>(nextEdge)].x;
                    // Only add join geometry at visible corners (not near-parallel edges).
                    needsJoin[static_cast<usize>(i)] = Abs(cross) >= 0.001f;
                }
            }

            if (antiAlias)
            {
                const Color transColor{ color.r, color.g, color.b, 0.0f };

                // Ring 0: outer fringe (left side, coverage=0).
                const u32 outerLeftBase = static_cast<u32>(vertices.Size());
                for (i32 i = 0; i < n; ++i)
                {
                    const Vec2 p = points[static_cast<usize>(i)];
                    const Vec2 bodyOffset = vertNormals[static_cast<usize>(i)] * halfWidth;
                    const Vec2 fringeOffset = unitNormals[static_cast<usize>(i)] * fringeWidth;
                    vertices.PushBack(VGVertex::Solid(p + bodyOffset + fringeOffset, transColor, 0.0f));
                }

                // Ring 1: stroke left edge (coverage=1).
                const u32 strokeLeftBase = static_cast<u32>(vertices.Size());
                for (i32 i = 0; i < n; ++i)
                {
                    const Vec2 p = points[static_cast<usize>(i)];
                    const Vec2 offset = vertNormals[static_cast<usize>(i)] * halfWidth;
                    vertices.PushBack(VGVertex::Solid(p + offset, color, 1.0f));
                }

                // Ring 2: stroke right edge (coverage=1).
                const u32 strokeRightBase = static_cast<u32>(vertices.Size());
                for (i32 i = 0; i < n; ++i)
                {
                    const Vec2 p = points[static_cast<usize>(i)];
                    const Vec2 offset = vertNormals[static_cast<usize>(i)] * halfWidth;
                    vertices.PushBack(VGVertex::Solid(p - offset, color, 1.0f));
                }

                // Ring 3: outer fringe (right side, coverage=0).
                const u32 outerRightBase = static_cast<u32>(vertices.Size());
                for (i32 i = 0; i < n; ++i)
                {
                    const Vec2 p = points[static_cast<usize>(i)];
                    const Vec2 bodyOffset = vertNormals[static_cast<usize>(i)] * halfWidth;
                    const Vec2 fringeOffset = unitNormals[static_cast<usize>(i)] * fringeWidth;
                    vertices.PushBack(VGVertex::Solid(p - bodyOffset - fringeOffset, transColor, 0.0f));
                }

                const i32 segCount = closed ? n : n - 1;
                for (i32 i = 0; i < segCount; ++i)
                {
                    const u32 ui = static_cast<u32>(i);
                    const u32 uj = static_cast<u32>((i + 1) % n);

                    indices.PushBack(outerLeftBase + ui); indices.PushBack(outerLeftBase + uj); indices.PushBack(strokeLeftBase + uj);
                    indices.PushBack(outerLeftBase + ui); indices.PushBack(strokeLeftBase + uj); indices.PushBack(strokeLeftBase + ui);

                    indices.PushBack(strokeLeftBase + ui); indices.PushBack(strokeLeftBase + uj); indices.PushBack(strokeRightBase + uj);
                    indices.PushBack(strokeLeftBase + ui); indices.PushBack(strokeRightBase + uj); indices.PushBack(strokeRightBase + ui);

                    indices.PushBack(strokeRightBase + ui); indices.PushBack(strokeRightBase + uj); indices.PushBack(outerRightBase + uj);
                    indices.PushBack(strokeRightBase + ui); indices.PushBack(outerRightBase + uj); indices.PushBack(outerRightBase + ui);
                }
            }
            else
            {
                // Without AA: simple left/right quad strip.
                const u32 strokeBase = static_cast<u32>(vertices.Size());
                for (i32 i = 0; i < n; ++i)
                {
                    const Vec2 p = points[static_cast<usize>(i)];
                    const Vec2 offset = vertNormals[static_cast<usize>(i)] * halfWidth;
                    vertices.PushBack(VGVertex::Solid(p + offset, color));
                    vertices.PushBack(VGVertex::Solid(p - offset, color));
                }

                const i32 segCount = closed ? n : n - 1;
                for (i32 i = 0; i < segCount; ++i)
                {
                    const i32 j = (i + 1) % n;
                    const u32 i0 = strokeBase + static_cast<u32>(i * 2);
                    const u32 i1 = strokeBase + static_cast<u32>(i * 2 + 1);
                    const u32 i2 = strokeBase + static_cast<u32>(j * 2);
                    const u32 i3 = strokeBase + static_cast<u32>(j * 2 + 1);

                    indices.PushBack(i0); indices.PushBack(i2); indices.PushBack(i1);
                    indices.PushBack(i1); indices.PushBack(i2); indices.PushBack(i3);
                }
            }

            // Add joins at vertices that need them.
            for (i32 i = 0; i < n; ++i)
            {
                if (!needsJoin[static_cast<usize>(i)]) continue;

                const i32 prevEdge = closed ? (i + edgeCount - 1) % edgeCount : i - 1;
                const i32 nextEdge = closed ? i % edgeCount : i;
                AddJoin(points[static_cast<usize>(i)], edgeNormals[static_cast<usize>(prevEdge)], edgeNormals[static_cast<usize>(nextEdge)],
                        halfWidth, style.join, color, vertices, indices);
            }

            // Add caps for open paths.
            if (!closed && style.cap != VGLineCap::Butt)
            {
                // Start cap.
                {
                    Vec2 dir = points[1] - points[0];
                    const f32 len = Length(dir);
                    if (len > 0.0001f)
                    {
                        dir = dir / len;
                        AddCap(points[0], -dir, edgeNormals[0], halfWidth, style.cap, color, vertices, indices);
                    }
                }
                // End cap.
                {
                    Vec2 dir = points[static_cast<usize>(n - 1)] - points[static_cast<usize>(n - 2)];
                    const f32 len = Length(dir);
                    if (len > 0.0001f)
                    {
                        dir = dir / len;
                        AddCap(points[static_cast<usize>(n - 1)], dir, edgeNormals[static_cast<usize>(edgeCount - 1)], halfWidth, style.cap, color, vertices, indices);
                    }
                }
            }
        }

        /// Compute miter-scaled and unit normals for a join vertex.
        static void ComputeJoinNormal(Vec2 prevNormal, Vec2 nextNormal, StrokeStyle style, f32 halfWidth, f32 minEdgeLen,
                                      Vec2& miterNormal, Vec2& unitNormal)
        {
            Vec2 avg = (prevNormal + nextNormal) * 0.5f;
            const f32 len = Length(avg);
            if (len < 0.0001f)
            {
                miterNormal = prevNormal;
                unitNormal = prevNormal;
                return;
            }

            avg = avg / len;
            unitNormal = avg;

            const f32 dot = prevNormal.x * avg.x + prevNormal.y * avg.y;
            if (dot > 0.0001f)
            {
                f32 miterLen = 1.0f / dot;

                // Clamp to prevent extreme miter spikes.
                miterLen = Min(miterLen, 600.0f);

                // Inner bevel: clamp if miter extends beyond the shorter adjacent edge.
                if (minEdgeLen > 0.0001f)
                {
                    const f32 limit = Max(1.01f, minEdgeLen / halfWidth);
                    if (miterLen > limit)
                        miterLen = limit;
                }

                if (miterLen > style.miterLimit && style.join == VGLineJoin::Miter)
                {
                    miterNormal = avg;
                    return;
                }

                miterNormal = avg * miterLen;
                return;
            }

            miterNormal = prevNormal;
            unitNormal = prevNormal;
        }

        static void AddJoin(Vec2 point, Vec2 prevNormal, Vec2 nextNormal, f32 halfWidth, VGLineJoin joinType,
                            Color color, Array<VGVertex>& vertices, Array<u32>& indices)
        {
            const f32 cross = prevNormal.x * nextNormal.y - prevNormal.y * nextNormal.x;
            if (Abs(cross) < 0.001f)
                return;

            if (joinType == VGLineJoin::Bevel)
            {
                const u32 baseIdx = static_cast<u32>(vertices.Size());
                vertices.PushBack(VGVertex::Solid(point, color));

                if (cross > 0.0f)
                {
                    vertices.PushBack(VGVertex::Solid(point + prevNormal * halfWidth, color));
                    vertices.PushBack(VGVertex::Solid(point + nextNormal * halfWidth, color));
                }
                else
                {
                    vertices.PushBack(VGVertex::Solid(point - prevNormal * halfWidth, color));
                    vertices.PushBack(VGVertex::Solid(point - nextNormal * halfWidth, color));
                }

                indices.PushBack(baseIdx);
                indices.PushBack(baseIdx + 1);
                indices.PushBack(baseIdx + 2);
            }
            else if (joinType == VGLineJoin::Round)
            {
                const f32 startAngle = Atan2(prevNormal.y, prevNormal.x);
                f32 endAngle = Atan2(nextNormal.y, nextNormal.x);

                if (cross > 0.0f)
                {
                    if (endAngle < startAngle) endAngle += kTwoPi;
                }
                else
                {
                    if (endAngle > startAngle) endAngle -= kTwoPi;
                }

                const i32 segments = Max(3, static_cast<i32>(Abs(endAngle - startAngle) * halfWidth * 0.5f));
                const f32 angleStep = (endAngle - startAngle) / static_cast<f32>(segments);

                const u32 baseIdx = static_cast<u32>(vertices.Size());
                vertices.PushBack(VGVertex::Solid(point, color));

                for (i32 i = 0; i <= segments; ++i)
                {
                    const f32 angle = startAngle + angleStep * static_cast<f32>(i);
                    const f32 x = point.x + Cos(angle) * halfWidth;
                    const f32 y = point.y + Sin(angle) * halfWidth;
                    vertices.PushBack(VGVertex::Solid(x, y, color));
                }

                for (i32 i = 0; i < segments; ++i)
                {
                    indices.PushBack(baseIdx);
                    indices.PushBack(baseIdx + static_cast<u32>(i + 1));
                    indices.PushBack(baseIdx + static_cast<u32>(i + 2));
                }
            }
        }

        static void AddCap(Vec2 point, Vec2 direction, Vec2 normal, f32 halfWidth, VGLineCap capType,
                           Color color, Array<VGVertex>& vertices, Array<u32>& indices)
        {
            if (capType == VGLineCap::Square)
            {
                const u32 baseIdx = static_cast<u32>(vertices.Size());
                const Vec2 p0 = point - normal * halfWidth;
                const Vec2 p1 = point + normal * halfWidth;
                const Vec2 p2 = point + direction * halfWidth + normal * halfWidth;
                const Vec2 p3 = point + direction * halfWidth - normal * halfWidth;

                vertices.PushBack(VGVertex::Solid(p0, color));
                vertices.PushBack(VGVertex::Solid(p1, color));
                vertices.PushBack(VGVertex::Solid(p2, color));
                vertices.PushBack(VGVertex::Solid(p3, color));

                indices.PushBack(baseIdx + 0);
                indices.PushBack(baseIdx + 1);
                indices.PushBack(baseIdx + 2);
                indices.PushBack(baseIdx + 0);
                indices.PushBack(baseIdx + 2);
                indices.PushBack(baseIdx + 3);
            }
            else if (capType == VGLineCap::Round)
            {
                const i32 segments = Max(4, static_cast<i32>(halfWidth * 0.5f));
                const u32 baseIdx = static_cast<u32>(vertices.Size());

                vertices.PushBack(VGVertex::Solid(point, color));

                const f32 startAngle = Atan2(normal.y, normal.x);
                const f32 angleStep = kPi / static_cast<f32>(segments);

                for (i32 i = 0; i <= segments; ++i)
                {
                    const f32 angle = startAngle + static_cast<f32>(i) * angleStep;
                    const f32 x = point.x + Cos(angle) * halfWidth;
                    const f32 y = point.y + Sin(angle) * halfWidth;
                    vertices.PushBack(VGVertex::Solid(x, y, color));
                }

                for (i32 i = 0; i < segments; ++i)
                {
                    indices.PushBack(baseIdx);
                    indices.PushBack(baseIdx + static_cast<u32>(i + 1));
                    indices.PushBack(baseIdx + static_cast<u32>(i + 2));
                }
            }
        }
    };
}
