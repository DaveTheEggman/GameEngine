/// Draconic::Render - the `:debug_draw` partition (Debug layer).
///
/// Instance-based immediate-mode debug drawing, ported from SedulousEngine's Sedulous.Renderer.Debug
/// DebugDraw. Game code accumulates world-space lines/triangles/wireframes + screen/3D text over a
/// frame; the debug passes flush them. Each `DebugDraw` is one accumulator - the RenderSubsystem owns a
/// GLOBAL one (drawn in every view) + one per SCENE (drawn only when that scene renders), which is what
/// keeps side-by-side scenes from bleeding. Every 3D method takes `overlay`: false = depth-tested,
/// true = always-on-top. Immediate-mode: Clear() once per frame.

module;
#include "Core/Prelude.h"

export module draconic.render:debug_draw;

import draconic.core;

using namespace draconic::core;

export namespace draconic::render::debug {

// World-space geometry vertex (16B): position + packed RGBA8 color (byte0=R -> matches Unorm8x4).
struct DebugVertex {
    Vector3 position;
    u32  color = 0xFFFFFFFFu;
};

// Screen/text vertex (24B): pixel-or-world position + font-atlas uv + packed color.
struct DebugTextVertex {
    Vector3 position;
    Vector2 uv;
    u32  color = 0xFFFFFFFFu;
};

enum class Debug2DKind : u8 { Text, Rectangle };

// One 2D overlay command (pixel-space text or filled rect). Text glyphs live in the list's char store.
struct Debug2DCommand {
    Debug2DKind kind = Debug2DKind::Text;
    Vector2 position{};     // pixels, top-left origin (negative x = right-aligned, see DrawScreenTextRight)
    Vector2 size{};         // pixels (rects)
    Color color{};
    i32  textStart = 0;
    i32  textLength = 0;
    f32  scale = 1.0f;
};

// One 3D text command (text anchored at a world position, projected to screen at render).
struct Debug3DTextCommand {
    Vector3 worldPos{};
    Color color{};
    i32  textStart = 0;
    i32  textLength = 0;
};

// Pack a float Color to RGBA8 with R in the low byte (so an Unorm8x4 vertex attribute reads R,G,B,A).
[[nodiscard]] inline u32 PackColor(const Color& c) noexcept {
    const auto b8 = [](f32 x) -> u32 { return static_cast<u32>(Clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b8(c.r) | (b8(c.g) << 8) | (b8(c.b) << 16) | (b8(c.a) << 24);
}

// Transform a point by a row-vector row-major matrix (mul(float4(p,1), m)) with perspective divide.
[[nodiscard]] inline Vector3 TransformPoint(const Matrix4& m, Vector3 p) noexcept {
    const f32 x = p.x * m(0, 0) + p.y * m(1, 0) + p.z * m(2, 0) + m(3, 0);
    const f32 y = p.x * m(0, 1) + p.y * m(1, 1) + p.z * m(2, 1) + m(3, 1);
    const f32 z = p.x * m(0, 2) + p.y * m(1, 2) + p.z * m(2, 2) + m(3, 2);
    const f32 w = p.x * m(0, 3) + p.y * m(1, 3) + p.z * m(2, 3) + m(3, 3);
    const f32 iw = (w != 0.0f) ? 1.0f / w : 1.0f;
    return Vector3{ x * iw, y * iw, z * iw };
}

class DebugDraw {
public:
    // --- read-only accessors for the passes ---
    [[nodiscard]] const Array<DebugVertex>&        LineVertices()        const noexcept { return m_lines; }
    [[nodiscard]] const Array<DebugVertex>&        OverlayLineVertices() const noexcept { return m_overlayLines; }
    [[nodiscard]] const Array<DebugVertex>&        TriVertices()         const noexcept { return m_tris; }
    [[nodiscard]] const Array<DebugVertex>&        OverlayTriVertices()  const noexcept { return m_overlayTris; }
    [[nodiscard]] const Array<Debug2DCommand>&     Commands2D()          const noexcept { return m_2d; }
    [[nodiscard]] const Array<Debug3DTextCommand>& TextCommands3D()      const noexcept { return m_3dText; }
    [[nodiscard]] const Array<u8>&                 TextChars()           const noexcept { return m_textChars; }

    [[nodiscard]] bool HasAnyDraws() const noexcept {
        return !m_lines.IsEmpty() || !m_overlayLines.IsEmpty() || !m_tris.IsEmpty() ||
               !m_overlayTris.IsEmpty() || !m_2d.IsEmpty() || !m_3dText.IsEmpty();
    }

    // Clears all accumulated draws. Called by the renderer once per frame.
    void Clear() noexcept {
        m_lines.Clear(); m_overlayLines.Clear(); m_tris.Clear(); m_overlayTris.Clear();
        m_2d.Clear(); m_3dText.Clear(); m_textChars.Clear();
    }

    // ==================== Lines ====================
    void DrawLine(Vector3 from, Vector3 to, Color color, bool overlay = false) {
        Array<DebugVertex>& list = overlay ? m_overlayLines : m_lines;
        const u32 c = PackColor(color);
        list.PushBack(DebugVertex{ from, c });
        list.PushBack(DebugVertex{ to, c });
    }
    void DrawLineOverlay(Vector3 from, Vector3 to, Color color) { DrawLine(from, to, color, true); }
    void DrawRay(Vector3 origin, Vector3 direction, Color color, bool overlay = false) { DrawLine(origin, origin + direction, color, overlay); }

    // ==================== Filled ====================
    void DrawTriangle(Vector3 v0, Vector3 v1, Vector3 v2, Color color, bool overlay = false) {
        Array<DebugVertex>& list = overlay ? m_overlayTris : m_tris;
        const u32 c = PackColor(color);
        list.PushBack(DebugVertex{ v0, c }); list.PushBack(DebugVertex{ v1, c }); list.PushBack(DebugVertex{ v2, c });
    }
    void DrawQuad(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, Color color, bool overlay = false) {
        DrawTriangle(v0, v1, v2, color, overlay); DrawTriangle(v0, v2, v3, color, overlay);
    }
    void DrawFilledBox(Vector3 mn, Vector3 mx, Color color, bool overlay = false) {
        const Vector3 v0{ mn.x, mn.y, mn.z }, v1{ mx.x, mn.y, mn.z }, v2{ mx.x, mn.y, mx.z }, v3{ mn.x, mn.y, mx.z };
        const Vector3 v4{ mn.x, mx.y, mn.z }, v5{ mx.x, mx.y, mn.z }, v6{ mx.x, mx.y, mx.z }, v7{ mn.x, mx.y, mx.z };
        DrawQuad(v0, v1, v2, v3, color, overlay);   // bottom
        DrawQuad(v4, v7, v6, v5, color, overlay);   // top
        DrawQuad(v0, v4, v5, v1, color, overlay);   // front
        DrawQuad(v2, v6, v7, v3, color, overlay);   // back
        DrawQuad(v0, v3, v7, v4, color, overlay);   // left
        DrawQuad(v1, v5, v6, v2, color, overlay);   // right
    }
    void DrawFilledBoxCenter(Vector3 center, Vector3 halfExtents, Color color, bool overlay = false) {
        DrawFilledBox(center - halfExtents, center + halfExtents, color, overlay);
    }

    // ==================== Wireframe ====================
    void DrawWireBox(Vector3 mn, Vector3 mx, Color color, bool overlay = false) {
        const Vector3 c000{ mn.x, mn.y, mn.z }, c100{ mx.x, mn.y, mn.z }, c010{ mn.x, mx.y, mn.z }, c110{ mx.x, mx.y, mn.z };
        const Vector3 c001{ mn.x, mn.y, mx.z }, c101{ mx.x, mn.y, mx.z }, c011{ mn.x, mx.y, mx.z }, c111{ mx.x, mx.y, mx.z };
        DrawLine(c000, c100, color, overlay); DrawLine(c100, c101, color, overlay); DrawLine(c101, c001, color, overlay); DrawLine(c001, c000, color, overlay);
        DrawLine(c010, c110, color, overlay); DrawLine(c110, c111, color, overlay); DrawLine(c111, c011, color, overlay); DrawLine(c011, c010, color, overlay);
        DrawLine(c000, c010, color, overlay); DrawLine(c100, c110, color, overlay); DrawLine(c101, c111, color, overlay); DrawLine(c001, c011, color, overlay);
    }
    void DrawWireBoxCenter(Vector3 center, Vector3 halfExtents, Color color, bool overlay = false) {
        DrawWireBox(center - halfExtents, center + halfExtents, color, overlay);
    }
    // Local AABB transformed by a world matrix (OBB).
    void DrawTransformedBox(Vector3 mn, Vector3 mx, const Matrix4& world, Color color, bool overlay = false) {
        Vector3 c[8];
        c[0] = TransformPoint(world, Vector3{ mn.x, mn.y, mn.z }); c[1] = TransformPoint(world, Vector3{ mx.x, mn.y, mn.z });
        c[2] = TransformPoint(world, Vector3{ mn.x, mx.y, mn.z }); c[3] = TransformPoint(world, Vector3{ mx.x, mx.y, mn.z });
        c[4] = TransformPoint(world, Vector3{ mn.x, mn.y, mx.z }); c[5] = TransformPoint(world, Vector3{ mx.x, mn.y, mx.z });
        c[6] = TransformPoint(world, Vector3{ mn.x, mx.y, mx.z }); c[7] = TransformPoint(world, Vector3{ mx.x, mx.y, mx.z });
        DrawLine(c[0], c[1], color, overlay); DrawLine(c[1], c[5], color, overlay); DrawLine(c[5], c[4], color, overlay); DrawLine(c[4], c[0], color, overlay);
        DrawLine(c[2], c[3], color, overlay); DrawLine(c[3], c[7], color, overlay); DrawLine(c[7], c[6], color, overlay); DrawLine(c[6], c[2], color, overlay);
        DrawLine(c[0], c[2], color, overlay); DrawLine(c[1], c[3], color, overlay); DrawLine(c[5], c[7], color, overlay); DrawLine(c[4], c[6], color, overlay);
    }
    void DrawCircle(Vector3 center, Vector3 u, Vector3 v, f32 radius, Color color, i32 segments = 32, bool overlay = false) {
        const Vector3 uN = Normalized(u), vN = Normalized(v);
        Vector3 prev = center + uN * radius;
        for (i32 i = 1; i <= segments; ++i) {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(segments) * kPi * 2.0f;
            const Vector3 point = center + uN * (radius * Cos(t)) + vN * (radius * Sin(t));
            DrawLine(prev, point, color, overlay);
            prev = point;
        }
    }
    void DrawCircleNormal(Vector3 center, f32 radius, Vector3 normal, Color color, i32 segments = 32, bool overlay = false) {
        const Vector3 up = (Abs(normal.y) < 0.99f) ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
        const Vector3 right = Normalized(Cross(up, normal));
        const Vector3 forward = Cross(normal, right);
        DrawCircle(center, right, forward, radius, color, segments, overlay);
    }
    void DrawWireSphere(Vector3 center, f32 radius, Color color, i32 segments = 24, bool overlay = false) {
        DrawCircle(center, Vector3{ 1, 0, 0 }, Vector3{ 0, 1, 0 }, radius, color, segments, overlay);
        DrawCircle(center, Vector3{ 0, 1, 0 }, Vector3{ 0, 0, 1 }, radius, color, segments, overlay);
        DrawCircle(center, Vector3{ 1, 0, 0 }, Vector3{ 0, 0, 1 }, radius, color, segments, overlay);
    }
    void DrawWireSphere(const BoundingSphere& sphere, Color color, i32 segments = 24, bool overlay = false) { DrawWireSphere(sphere.center, sphere.radius, color, segments, overlay); }
    void DrawWireSphereOverlay(Vector3 center, f32 radius, Color color, i32 segments = 24) { DrawWireSphere(center, radius, color, segments, true); }
    void DrawCircleOverlay(Vector3 center, Vector3 u, Vector3 v, f32 radius, Color color, i32 segments = 32) { DrawCircle(center, u, v, radius, color, segments, true); }
    // Wireframe capsule (cylinder body + two hemisphere caps).
    void DrawCapsule(Vector3 center, f32 radius, f32 height, Color color, i32 segments = 16, bool overlay = false) {
        const f32 halfHeight = height * 0.5f - radius;
        const Vector3 top = center + Vector3{ 0, halfHeight, 0 }, bottom = center - Vector3{ 0, halfHeight, 0 };
        const f32 step = kPi * 2.0f / static_cast<f32>(segments);
        for (i32 i = 0; i < segments; ++i) {   // vertical lines + the two end circles
            const f32 a0 = static_cast<f32>(i) * step, a1 = static_cast<f32>(i + 1) * step;
            const Vector3 o0{ Cos(a0) * radius, 0, Sin(a0) * radius }, o1{ Cos(a1) * radius, 0, Sin(a1) * radius };
            DrawLine(top + o0, bottom + o0, color, overlay);
            DrawLine(top + o0, top + o1, color, overlay);
            DrawLine(bottom + o0, bottom + o1, color, overlay);
        }
        const i32 halfSeg = segments / 2;
        const f32 halfStep = kPi / static_cast<f32>(halfSeg);
        for (i32 i = 0; i < halfSeg; ++i) {     // hemisphere arcs (XY + ZY planes, top + bottom)
            const f32 a0 = static_cast<f32>(i) * halfStep, a1 = static_cast<f32>(i + 1) * halfStep;
            DrawLine(top + Vector3{ Sin(a0) * radius, Cos(a0) * radius, 0 }, top + Vector3{ Sin(a1) * radius, Cos(a1) * radius, 0 }, color, overlay);
            DrawLine(top + Vector3{ 0, Cos(a0) * radius, Sin(a0) * radius }, top + Vector3{ 0, Cos(a1) * radius, Sin(a1) * radius }, color, overlay);
            DrawLine(bottom + Vector3{ Sin(a0) * radius, -Cos(a0) * radius, 0 }, bottom + Vector3{ Sin(a1) * radius, -Cos(a1) * radius, 0 }, color, overlay);
            DrawLine(bottom + Vector3{ 0, -Cos(a0) * radius, Sin(a0) * radius }, bottom + Vector3{ 0, -Cos(a1) * radius, Sin(a1) * radius }, color, overlay);
        }
    }
    // World basis axes of a transform (red=X, green=Y, blue=Z). Rows are the basis (row-vector convention).
    void DrawAxis(const Matrix4& transform, f32 size = 1.0f, bool overlay = false) {
        const Vector3 o{ transform(3, 0), transform(3, 1), transform(3, 2) };
        const Vector3 x{ transform(0, 0), transform(0, 1), transform(0, 2) };
        const Vector3 y{ transform(1, 0), transform(1, 1), transform(1, 2) };
        const Vector3 z{ transform(2, 0), transform(2, 1), transform(2, 2) };
        DrawLine(o, o + x * size, Color{ 1, 0, 0, 1 }, overlay);
        DrawLine(o, o + y * size, Color{ 0, 1, 0, 1 }, overlay);
        DrawLine(o, o + z * size, Color{ 0, 0, 1, 1 }, overlay);
    }
    void DrawCross(Vector3 center, f32 size, Color color, bool overlay = false) {
        const f32 h = size * 0.5f;
        DrawLine(center - Vector3{ h, 0, 0 }, center + Vector3{ h, 0, 0 }, color, overlay);
        DrawLine(center - Vector3{ 0, h, 0 }, center + Vector3{ 0, h, 0 }, color, overlay);
        DrawLine(center - Vector3{ 0, 0, h }, center + Vector3{ 0, 0, h }, color, overlay);
    }
    void DrawArrow(Vector3 start, Vector3 end, Color color, f32 headSize = 0.1f, bool overlay = false) {
        DrawLine(start, end, color, overlay);
        const Vector3 dir = Normalized(end - start);
        const Vector3 perp1 = Normalized(Cross(dir, (Abs(dir.y) < 0.99f) ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 }));
        const Vector3 perp2 = Cross(dir, perp1);
        const Vector3 headBase = end - dir * headSize;
        const f32  hr = headSize * 0.5f;
        DrawLine(end, headBase + perp1 * hr, color, overlay); DrawLine(end, headBase - perp1 * hr, color, overlay);
        DrawLine(end, headBase + perp2 * hr, color, overlay); DrawLine(end, headBase - perp2 * hr, color, overlay);
    }
    void DrawGrid(Vector3 center, f32 size, i32 divisions, Color color, bool overlay = false) {
        const f32 half = size * 0.5f, step = size / static_cast<f32>(divisions);
        for (i32 i = 0; i <= divisions; ++i) {
            const f32 t = static_cast<f32>(i) * step - half;
            DrawLine(center + Vector3{ -half, 0, t }, center + Vector3{ half, 0, t }, color, overlay);
            DrawLine(center + Vector3{ t, 0, -half }, center + Vector3{ t, 0, half }, color, overlay);
        }
    }
    void DrawCylinder(Vector3 center, f32 radius, f32 height, Color color, i32 segments = 16, bool overlay = false) {
        const f32 hh = height * 0.5f, step = kPi * 2.0f / static_cast<f32>(segments);
        const Vector3 top = center + Vector3{ 0, hh, 0 }, bottom = center - Vector3{ 0, hh, 0 };
        for (i32 i = 0; i < segments; ++i) {
            const f32 a0 = static_cast<f32>(i) * step, a1 = static_cast<f32>(i + 1) * step;
            const Vector3 o0{ Cos(a0) * radius, 0, Sin(a0) * radius }, o1{ Cos(a1) * radius, 0, Sin(a1) * radius };
            DrawLine(top + o0, bottom + o0, color, overlay);
            DrawLine(top + o0, top + o1, color, overlay);
            DrawLine(bottom + o0, bottom + o1, color, overlay);
        }
    }
    void DrawCone(Vector3 apex, Vector3 direction, f32 length, f32 angle, Color color, i32 segments = 16, bool overlay = false) {
        const Vector3 dir = Normalized(direction);
        const Vector3 baseCenter = apex + dir * length;
        const f32  radius = length * (Sin(angle) / Cos(angle));   // tan(angle)
        const Vector3 up = (Abs(dir.y) < 0.99f) ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
        const Vector3 right = Normalized(Cross(up, dir)), forward = Cross(dir, right);
        const f32  step = kPi * 2.0f / static_cast<f32>(segments);
        for (i32 i = 0; i < segments; ++i) {
            const f32 a0 = static_cast<f32>(i) * step, a1 = static_cast<f32>(i + 1) * step;
            const Vector3 p0 = baseCenter + (right * Cos(a0) + forward * Sin(a0)) * radius;
            const Vector3 p1 = baseCenter + (right * Cos(a1) + forward * Sin(a1)) * radius;
            DrawLine(p0, p1, color, overlay); DrawLine(apex, p0, color, overlay);
        }
    }
    // Camera frustum edges from an inverse view-projection (NDC z in [0,1]).
    void DrawFrustum(const Matrix4& invViewProj, Color color, bool overlay = false) {
        Vector3 corners[8]; i32 idx = 0;
        for (i32 z = 0; z < 2; ++z) for (i32 y = 0; y < 2; ++y) for (i32 x = 0; x < 2; ++x) {
            corners[idx++] = TransformPoint(invViewProj, Vector3{ x == 0 ? -1.0f : 1.0f, y == 0 ? -1.0f : 1.0f, static_cast<f32>(z) });
        }
        DrawLine(corners[0], corners[1], color, overlay); DrawLine(corners[1], corners[3], color, overlay);
        DrawLine(corners[3], corners[2], color, overlay); DrawLine(corners[2], corners[0], color, overlay);
        DrawLine(corners[4], corners[5], color, overlay); DrawLine(corners[5], corners[7], color, overlay);
        DrawLine(corners[7], corners[6], color, overlay); DrawLine(corners[6], corners[4], color, overlay);
        DrawLine(corners[0], corners[4], color, overlay); DrawLine(corners[1], corners[5], color, overlay);
        DrawLine(corners[2], corners[6], color, overlay); DrawLine(corners[3], corners[7], color, overlay);
    }

    // ==================== Text + 2D ====================
    void DrawText3D(Vector3 worldPos, StringView text, Color color) {
        if (text.IsEmpty()) { return; }
        const i32 start = static_cast<i32>(m_textChars.Size());
        AppendChars(text);
        m_3dText.PushBack(Debug3DTextCommand{ worldPos, color, start, static_cast<i32>(text.Size()) });
    }
    void DrawScreenText(f32 x, f32 y, StringView text, Color color, f32 scale = 1.0f) {
        if (text.IsEmpty()) { return; }
        const i32 start = static_cast<i32>(m_textChars.Size());
        AppendChars(text);
        m_2d.PushBack(Debug2DCommand{ Debug2DKind::Text, Vector2{ x, y }, Vector2{ 0, 0 }, color, start, static_cast<i32>(text.Size()), scale });
    }
    void DrawScreenTextRight(f32 rightMargin, f32 y, StringView text, Color color, f32 scale = 1.0f) {
        if (text.IsEmpty()) { return; }
        const i32 start = static_cast<i32>(m_textChars.Size());
        AppendChars(text);
        // negative x signals right-aligned (see the screen pass).
        m_2d.PushBack(Debug2DCommand{ Debug2DKind::Text, Vector2{ -(rightMargin + 1.0f), y }, Vector2{ 0, 0 }, color, start, static_cast<i32>(text.Size()), scale });
    }
    void DrawScreenRect(f32 x, f32 y, f32 width, f32 height, Color color) {
        m_2d.PushBack(Debug2DCommand{ Debug2DKind::Rectangle, Vector2{ x, y }, Vector2{ width, height }, color, 0, 0, 1.0f });
    }

private:
    void AppendChars(StringView text) {
        const utf8char* d = text.Data();
        for (usize i = 0; i < text.Size(); ++i) { m_textChars.PushBack(static_cast<u8>(d[i])); }
    }

    Array<DebugVertex>        m_lines;
    Array<DebugVertex>        m_overlayLines;
    Array<DebugVertex>        m_tris;
    Array<DebugVertex>        m_overlayTris;
    Array<Debug2DCommand>     m_2d;
    Array<Debug3DTextCommand> m_3dText;
    Array<u8>                 m_textChars;
};

} // namespace draconic::render::debug
