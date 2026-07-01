/// Draconic::Geometry — the `:primitives` partition.
///
/// Procedural primitive meshes (debug shapes / placeholders / tests). Each returns a
/// fully-formed StaticMesh — static vertex stream + 32-bit indices + one submesh +
/// generated tangents + bounds. These build the typed StaticMesh directly (no generic
/// untyped vertex-buffer indirection).

module;
#include "Core/Prelude.h"

export module draconic.geometry:primitives;

import draconic.core;
import :types;
import :mesh;

using namespace draconic::core;

export namespace draconic::geometry {

class Primitives {
public:
    // A unit quad in the XY plane (two triangles), facing +Z.
    [[nodiscard]] static RefPtr<StaticMesh> Quad(f32 width = 1.0f, f32 height = 1.0f) {
        RefPtr<StaticMesh> mesh = MakeRef<StaticMesh>(DefaultAllocator());
        const f32 hw = width * 0.5f, hh = height * 0.5f;
        const u32 white = 0xFFFFFFFFu;
        mesh->vertices.PushBack(StaticMeshVertex{ Vec3{ -hw, -hh, 0 }, Vec3{ 0, 0, 1 }, Vec2{ 0, 1 }, white, Vec3{ 1, 0, 0 } });
        mesh->vertices.PushBack(StaticMeshVertex{ Vec3{  hw, -hh, 0 }, Vec3{ 0, 0, 1 }, Vec2{ 1, 1 }, white, Vec3{ 1, 0, 0 } });
        mesh->vertices.PushBack(StaticMeshVertex{ Vec3{  hw,  hh, 0 }, Vec3{ 0, 0, 1 }, Vec2{ 1, 0 }, white, Vec3{ 1, 0, 0 } });
        mesh->vertices.PushBack(StaticMeshVertex{ Vec3{ -hw,  hh, 0 }, Vec3{ 0, 0, 1 }, Vec2{ 0, 0 }, white, Vec3{ 1, 0, 0 } });
        const u32 quad[6] = { 0, 1, 2, 0, 2, 3 };
        mesh->indices.Resize(6);
        for (u32 i : quad) { mesh->indices.Add(i); }
        Finish(*mesh);
        return mesh;
    }

    // An axis-aligned cube of edge `size`, 24 verts (hard per-face normals).
    [[nodiscard]] static RefPtr<StaticMesh> Cube(f32 size = 1.0f) {
        RefPtr<StaticMesh> mesh = MakeRef<StaticMesh>(DefaultAllocator());
        const f32 h = size * 0.5f;
        mesh->indices.Resize(36);   // 6 faces x 2 triangles x 3 indices
        // 6 faces: (origin corner, edge-u, edge-v, normal)
        AddFace(*mesh, Vec3{ -h, -h,  h }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Vec3{ 0, 0, 1 }, size);   // +Z
        AddFace(*mesh, Vec3{  h, -h, -h }, Vec3{ -1, 0, 0 }, Vec3{ 0, 1, 0 }, Vec3{ 0, 0, -1 }, size);  // -Z
        AddFace(*mesh, Vec3{  h, -h,  h }, Vec3{ 0, 0, -1 }, Vec3{ 0, 1, 0 }, Vec3{ 1, 0, 0 }, size);   // +X
        AddFace(*mesh, Vec3{ -h, -h, -h }, Vec3{ 0, 0, 1 }, Vec3{ 0, 1, 0 }, Vec3{ -1, 0, 0 }, size);   // -X
        AddFace(*mesh, Vec3{ -h,  h,  h }, Vec3{ 1, 0, 0 }, Vec3{ 0, 0, -1 }, Vec3{ 0, 1, 0 }, size);   // +Y
        AddFace(*mesh, Vec3{ -h, -h, -h }, Vec3{ 1, 0, 0 }, Vec3{ 0, 0, 1 }, Vec3{ 0, -1, 0 }, size);   // -Y
        Finish(*mesh);
        return mesh;
    }

    // A flat grid in the XZ plane, `width` x `depth`, subdivided.
    [[nodiscard]] static RefPtr<StaticMesh> Plane(f32 width = 1.0f, f32 depth = 1.0f, u32 xSegments = 1, u32 zSegments = 1) {
        RefPtr<StaticMesh> mesh = MakeRef<StaticMesh>(DefaultAllocator());
        const u32 xs = xSegments < 1 ? 1 : xSegments, zs = zSegments < 1 ? 1 : zSegments;
        const u32 white = 0xFFFFFFFFu;
        for (u32 z = 0; z <= zs; ++z) {
            for (u32 x = 0; x <= xs; ++x) {
                const f32 u = static_cast<f32>(x) / static_cast<f32>(xs);
                const f32 v = static_cast<f32>(z) / static_cast<f32>(zs);
                mesh->vertices.PushBack(StaticMeshVertex{
                    Vec3{ (u - 0.5f) * width, 0.0f, (v - 0.5f) * depth }, Vec3{ 0, 1, 0 }, Vec2{ u, v }, white, Vec3{ 1, 0, 0 } });
            }
        }
        mesh->indices.Resize(xs * zs * 6);
        const u32 rowStride = xs + 1;
        for (u32 z = 0; z < zs; ++z) {
            for (u32 x = 0; x < xs; ++x) {
                const u32 i0 = z * rowStride + x, i1 = i0 + 1, i2 = i0 + rowStride, i3 = i2 + 1;
                mesh->indices.AddTriangle(i0, i2, i1);
                mesh->indices.AddTriangle(i1, i2, i3);
            }
        }
        Finish(*mesh);
        return mesh;
    }

    // A UV sphere of `radius` with `segments` longitudes and `rings` latitudes.
    [[nodiscard]] static RefPtr<StaticMesh> Sphere(f32 radius = 0.5f, u32 segments = 32, u32 rings = 16) {
        RefPtr<StaticMesh> mesh = MakeRef<StaticMesh>(DefaultAllocator());
        const u32 seg = segments < 3 ? 3 : segments, rng = rings < 2 ? 2 : rings;
        const u32 white = 0xFFFFFFFFu;
        for (u32 r = 0; r <= rng; ++r) {
            const f32 v = static_cast<f32>(r) / static_cast<f32>(rng);
            const f32 phi = v * kPi;                       // 0..pi (pole to pole)
            const f32 sinPhi = Sin(phi), cosPhi = Cos(phi);
            for (u32 s = 0; s <= seg; ++s) {
                const f32 u = static_cast<f32>(s) / static_cast<f32>(seg);
                const f32 theta = u * 2.0f * kPi;
                const Vec3 n{ Cos(theta) * sinPhi, cosPhi, Sin(theta) * sinPhi };
                mesh->vertices.PushBack(StaticMeshVertex{ n * radius, n, Vec2{ u, v }, white, Vec3{ 1, 0, 0 } });
            }
        }
        mesh->indices.Resize(seg * rng * 6);
        const u32 rowStride = seg + 1;
        for (u32 r = 0; r < rng; ++r) {
            for (u32 s = 0; s < seg; ++s) {
                const u32 i0 = r * rowStride + s, i1 = i0 + 1, i2 = i0 + rowStride, i3 = i2 + 1;
                // CCW-from-outside winding (matches SedulousEngine CreateSphere: (a,b,c),(b,d,c) with
                // a=i0,b=i1,c=i2,d=i3). The port had (i0,i2,i1)/(i1,i2,i3) — last two swapped — which
                // reversed the front face to inward, so back-face culling hid the outer shell.
                mesh->indices.AddTriangle(i0, i1, i2);
                mesh->indices.AddTriangle(i1, i3, i2);
            }
        }
        Finish(*mesh);
        return mesh;
    }

private:
    // Adds a quad face (4 verts, 2 tris) anchored at `origin`, spanning `size` along
    // unit edges `eu`/`ev`, with face normal `n`. The caller pre-sizes the index
    // buffer; indices append through its cursor.
    static void AddFace(StaticMesh& mesh, Vec3 origin, Vec3 eu, Vec3 ev, Vec3 n, f32 size) {
        const u32 base = mesh.VertexCount();
        const u32 white = 0xFFFFFFFFu;
        const Vec3 u = eu * size, v = ev * size;
        mesh.vertices.PushBack(StaticMeshVertex{ origin,             n, Vec2{ 0, 1 }, white, Vec3{ 1, 0, 0 } });
        mesh.vertices.PushBack(StaticMeshVertex{ origin + u,         n, Vec2{ 1, 1 }, white, Vec3{ 1, 0, 0 } });
        mesh.vertices.PushBack(StaticMeshVertex{ origin + u + v,     n, Vec2{ 1, 0 }, white, Vec3{ 1, 0, 0 } });
        mesh.vertices.PushBack(StaticMeshVertex{ origin + v,         n, Vec2{ 0, 0 }, white, Vec3{ 1, 0, 0 } });
        mesh.indices.AddTriangle(base, base + 1, base + 2);
        mesh.indices.AddTriangle(base, base + 2, base + 3);
    }

    static void Finish(StaticMesh& mesh) {
        mesh.GenerateTangents();
        mesh.CalculateBounds();
        mesh.subMeshes.PushBack(SubMesh{ 0, static_cast<i32>(mesh.IndexCount()), 0, PrimitiveType::Triangles });
    }
};

} // namespace draconic::geometry
