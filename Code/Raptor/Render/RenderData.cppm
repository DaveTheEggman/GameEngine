/// Raptor::Render — the `:data` partition.
///
/// The render-data contract — and the boundary that keeps the renderer scene-agnostic.
/// Render data is *extracted and pushed to* the renderer; the renderer never reaches back
/// into a scene (one-way: the scene-integration layer in raptor.render.subsystem depends
/// on this, not the reverse).
///
/// A `RenderData` is a unit of renderable work: a `RenderCategory` tag plus the data a
/// draw needs (e.g. `MeshRenderData` = world matrix + mesh + material). It is allocated
/// from a per-frame `FrameArena` (bump allocator), is trivially destructible, and is valid
/// for exactly one frame. An `ExtractedScene` is the per-scene, once-per-frame, immutable
/// snapshot of all a scene's render data; every view of that scene shares it read-only.
///
/// A `RenderData` carries no view-dependent state: the sort key (which depends on the
/// camera) lives on a per-view `DrawItem`, computed during the view's cull+sort against the
/// shared snapshot. (§5/§9 of docs/design/renderer.md.)

module;
#include "Core/Prelude.h"
#include <new>
#include <type_traits>

export module raptor.render:data;

import raptor.core;
import raptor.geometry;
import raptor.materials;

using namespace raptor::core;

export namespace raptor::render {

// A renderable's category — the dispatch key that routes it to a `Renderer`. A plain u16
// (not an enum class) so external subsystems (particles, world-space UI) can claim ids
// beyond the built-ins without touching this enum. Values >= kBuiltinCategoryCount are
// available to extensions; the `Renderer` registry sizes its table to kMaxCategories.
using RenderCategory = u16;

namespace RenderCategories {
    inline constexpr RenderCategory Opaque         = 0;   // depth-sorted front-to-back
    inline constexpr RenderCategory Masked         = 1;   // alpha-tested, opaque-ish
    inline constexpr RenderCategory Transparent    = 2;   // depth-sorted back-to-front, blended
    inline constexpr RenderCategory Sky            = 3;
    inline constexpr RenderCategory Decal          = 4;
    inline constexpr RenderCategory Light          = 5;
    inline constexpr RenderCategory ReflectionProbe= 6;
    inline constexpr RenderCategory GUI            = 7;
    inline constexpr RenderCategory Particle       = 8;
}
inline constexpr u16 kBuiltinCategoryCount = 9;
inline constexpr u16 kMaxCategories        = 64;   // registry table size (room for extensions)

// Base for a unit of renderable work. Arena-allocated, trivially destructible, valid one
// frame. Dispatch is by `category` (not virtual) — the registered `Renderer` knows the
// concrete subclass and static_casts, so there is no vtable.
struct RenderData {
    RenderCategory category = RenderCategories::Opaque;
};

// One mesh draw: a mesh + material at a world transform. Pointers are borrowed for the
// frame (the producer keeps the resources alive). `worldCenter` is the world-space bounds
// center, used for view-depth sorting (and, later, culling). `entityId` is an opaque tag
// the producer may set (e.g. a packed entity handle) for picking — meaningless to the core.
struct MeshRenderData : RenderData {
    Mat4                  world       = Mat4::Identity();
    Vec3                  worldCenter = Vec3{ 0, 0, 0 };
    Color                 color       = Color{ 1.0f, 1.0f, 1.0f, 1.0f };   // per-instance tint
    geometry::StaticMesh* mesh        = nullptr;
    materials::Material*  material     = nullptr;
    u64                   entityId    = 0;
};
static_assert(std::is_trivially_destructible_v<MeshRenderData>);

// One light, packed for a GPU storage buffer (std430, 64 bytes = 4x float4). A shading input,
// not a drawable — extracted into the ExtractedScene's light list, uploaded to a storage buffer,
// and consumed by the forward shading loop. Directional: dir is the light direction; Point/Spot:
// position + range (+ spot cone cosines). type: 0 = Directional, 1 = Point, 2 = Spot.
struct GpuLight {
    Vec3 positionWS = Vec3{ 0, 0, 0 };   f32 range     = 0.0f;   // xyz pos, w range
    Vec3 color      = Vec3{ 1, 1, 1 };   f32 intensity = 1.0f;   // rgb color, a intensity
    Vec3 directionWS= Vec3{ 0, -1, 0 };  f32 type      = 0.0f;   // xyz dir, w type
    f32  innerCos = 1.0f; f32 outerCos = 1.0f;                   // spot cone cosines
    // shadowIndex: -1 = this light casts no shadow; else an index into the shadow data (phase 5.1
    // has a single directional shadow map, so any >= 0 selects it). pad1 reserved (cascade count).
    f32  shadowIndex = -1.0f; f32 pad1 = 0.0f;
};
static_assert(sizeof(GpuLight) == 64);

// The active directional shadow caster for a scene (the extraction OUTPUT): just the light direction
// + whether one exists. The cascade matrices are derived later (in RenderFrame, where the camera
// frustum is available) since CSM fitting needs the camera. valid == false => no shadow this frame.
struct DirectionalShadow {
    Vec3 direction = Vec3{ 0, -1, 0 };
    bool valid     = false;
};

// Cascaded shadow map data for the directional caster (phase 5.2), computed per-frame from the
// primary view's frustum + the light direction. kCount cascades, each a world->light-clip matrix +
// the view-space depth where it ends (cascade selection) + the world size of one shadow texel (for
// normal-offset bias). Shared by all views in 5.2 (fit to the primary camera).
struct ShadowCascades {
    static constexpr u32 kCount = 4;
    Mat4 viewProj[kCount]       = { Mat4::Identity(), Mat4::Identity(), Mat4::Identity(), Mat4::Identity() };
    f32  splitFar[kCount]       = { 0.0f, 0.0f, 0.0f, 0.0f };   // view-space far depth of each cascade
    f32  texelWorldSize[kCount] = { 0.0f, 0.0f, 0.0f, 0.0f };   // world units per texel (normal-offset bias)
    bool valid                  = false;
};

// One local-light (spot, or a single point-light cube face) shadow entry, packed for a
// StructuredBuffer (96 bytes). `GpuLight.shadowIndex` selects the entry (point lights use 6
// consecutive face entries). `atlasScaleBias` maps the light-clip NDC into the light's tile in the
// shared shadow atlas: uv_atlas = uv_ndc * scale + offset. Built per-frame by the ShadowSystem once
// the atlas layout is known (so it carries the assigned tile), unlike the directional cascades.
struct GpuLocalShadow {
    Mat4 viewProj       = Mat4::Identity();             // world -> light clip (perspective)
    Vec4 atlasScaleBias = Vec4{ 1, 1, 0, 0 };           // xy = uv scale, zw = uv offset (tile in atlas)
    f32  depthBias      = 0.0015f;                      // constant depth-compare bias
    f32  pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(GpuLocalShadow) == 96);

// A spot/point light that should cast a shadow — the extraction OUTPUT. The ShadowSystem assigns it
// atlas tile(s) and builds its perspective view-projection(s) at frame time (when the atlas layout is
// known), then patches the source light's shadowIndex to point at the built entry. type mirrors
// GpuLight (1 = point, 2 = spot); point lights expand to 6 cube faces in 5.3b.
struct LocalShadowCaster {
    u32  type        = 2;                  // 1 = point, 2 = spot
    Vec3 positionWS  = Vec3{ 0, 0, 0 };
    Vec3 directionWS = Vec3{ 0, -1, 0 };
    f32  range       = 10.0f;              // perspective far plane
    f32  outerAngle  = 0.6f;              // spot cone half-angle (radians); fov = 2 * outerAngle
};

// Atlas tile budget for local (spot/point) shadows per frame. A spot consumes 1 tile, a point 6
// (cube faces). Extraction assigns each caster's shadowIndex = its BASE tile (0-based, into the
// GpuLocalShadow buffer) and caps total tiles here; the ShadowSystem's atlas must hold this many.
inline constexpr u32 kMaxLocalShadowTiles = 16;

// A per-view draw entry: a sort key (computed against the view's camera) + the shared
// render data it refers to. The per-view draw list is an Array<DrawItem> the renderer sorts
// (radix) then walks. RenderData is borrowed from the ExtractedScene (immutable snapshot).
struct DrawItem {
    u64               key  = 0;
    const RenderData* data = nullptr;
};

// ---- sort keys -------------------------------------------------------------------------
//
// 64-bit key, MSB-first significance so a single ascending radix sort yields the desired
// order: [category:16][state:24][depth:24]. Category groups draws by Renderer; `state`
// (material/PSO identity) clusters same-pipeline draws to minimize state changes; `depth`
// orders within that — front-to-back for opaque (early-Z), back-to-front for transparent
// (correct blending). The producer inverts depth for transparent before packing.

inline constexpr u32 kSortDepthBits = 24;
inline constexpr u32 kSortStateBits = 24;

[[nodiscard]] inline u64 MakeSortKey(RenderCategory category, u32 stateBits, u32 depthBits) noexcept {
    const u64 cat   = static_cast<u64>(category);
    const u64 state = static_cast<u64>(stateBits) & ((1ull << kSortStateBits) - 1);
    const u64 depth = static_cast<u64>(depthBits) & ((1ull << kSortDepthBits) - 1);
    return (cat << (kSortStateBits + kSortDepthBits)) | (state << kSortDepthBits) | depth;
}

// Quantize a normalized [0,1] depth to the 24-bit depth field. `invert` for back-to-front.
[[nodiscard]] inline u32 QuantizeDepth(f32 depth01, bool invert) noexcept {
    f32 d = depth01 < 0.0f ? 0.0f : (depth01 > 1.0f ? 1.0f : depth01);
    if (invert) { d = 1.0f - d; }
    constexpr u32 kMax = (1u << kSortDepthBits) - 1;
    return static_cast<u32>(d * static_cast<f32>(kMax));
}

// ---- frame arena -----------------------------------------------------------------------
//
// A growable, chunked bump allocator for one frame's RenderData. Allocations are valid
// until Reset() (which keeps the chunks for reuse next frame — no per-frame churn). Only
// trivially-destructible types (RenderData subclasses) are allocated, so Reset() reclaims
// without running destructors. (Phase 2 swaps this for the double-buffered RenderContext
// with per-worker arenas; the New<T>/Reset contract stays.)
class FrameArena {
public:
    explicit FrameArena(usize chunkSize = kDefaultChunkSize) noexcept : m_chunkSize(chunkSize) {}
    ~FrameArena() { for (Chunk& c : m_chunks) { DefaultAllocator().Free(c.data); } }

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    template <typename T, typename... Args>
    [[nodiscard]] T* New(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena types must be trivially destructible");
        void* p = Allocate(sizeof(T), alignof(T));
        return p != nullptr ? new (p) T{ static_cast<Args&&>(args)... } : nullptr;
    }

    [[nodiscard]] void* Allocate(usize size, usize alignment) {
        // Walk to a chunk that fits (reusing chunks retained across Reset), else grow.
        for (;;) {
            if (m_current < m_chunks.Size()) {
                Chunk& c = m_chunks[m_current];
                const usize base    = reinterpret_cast<usize>(c.data);
                const usize aligned = AlignUp(base + m_offset, alignment) - base;
                if (aligned + size <= c.size) {
                    m_offset = aligned + size;
                    return c.data + aligned;
                }
                // doesn't fit this chunk — advance to the next
                ++m_current;
                m_offset = 0;
                continue;
            }
            if (!AddChunk(size > m_chunkSize ? size : m_chunkSize)) { return nullptr; }
        }
    }

    void Reset() noexcept { m_current = 0; m_offset = 0; }

    [[nodiscard]] usize ChunkCount() const noexcept { return m_chunks.Size(); }

private:
    static constexpr usize kDefaultChunkSize = 64 * 1024;
    static constexpr usize kChunkAlign       = 16;   // >= any RenderData alignment (Mat4 = 16)

    struct Chunk { byte* data = nullptr; usize size = 0; };

    bool AddChunk(usize size) {
        void* mem = DefaultAllocator().Allocate(size, kChunkAlign);
        if (mem == nullptr) { return false; }
        m_chunks.PushBack(Chunk{ static_cast<byte*>(mem), size });
        return true;
    }

    Array<Chunk> m_chunks;
    usize        m_chunkSize;
    usize        m_current = 0;   // index of the chunk being filled
    usize        m_offset  = 0;   // bump cursor within m_chunks[m_current]
};

// ---- extracted scene -------------------------------------------------------------------
//
// The per-scene, once-per-frame, immutable snapshot pushed to the renderer: world-space
// render data for one scene. Views of the same scene share it read-only (N cameras = 1
// extraction). Lights + environment land in later phases; phase 1 carries renderables.
class ExtractedScene {
public:
    // Allocate a RenderData subclass from the arena and register it in the snapshot.
    template <typename T, typename... Args>
    [[nodiscard]] T* Add(Args&&... args) {
        T* p = m_arena.New<T>(static_cast<Args&&>(args)...);
        if (p != nullptr) { m_items.PushBack(static_cast<RenderData*>(p)); }
        return p;
    }

    // Adopt an externally-allocated RenderData into the snapshot (the data must outlive this
    // snapshot's use — e.g. it lives in a RenderContext per-worker arena owned by the producer).
    // Used by parallel extraction: workers fill their own arenas, then the merge adopts the
    // pointers here single-threaded.
    void AddExternal(RenderData* data) { if (data != nullptr) { m_items.PushBack(data); } }

    // Add a light to the snapshot (shading input, not a drawable).
    void AddLight(const GpuLight& light) { m_lights.PushBack(light); }

    // Register a spot/point light as a shadow caster (the ShadowSystem builds its atlas tile + matrix
    // at frame time). `lightIndex` must equal the light's position in the list (the index AddLight
    // assigns) so its shadowIndex can be patched once the atlas slot is known.
    void AddLocalShadowCaster(const LocalShadowCaster& c) { m_localCasters.PushBack(c); }
    [[nodiscard]] Span<const LocalShadowCaster> LocalShadowCasters() const noexcept {
        return Span<const LocalShadowCaster>{ m_localCasters.Data(), m_localCasters.Size() };
    }

    // The scene's environment ambient (a flat indirect term until IBL lands). Premultiplied
    // color × intensity, applied as `albedo * ambient` in the forward shader.
    void SetAmbient(const Vec3& ambient) noexcept { m_ambient = ambient; }
    [[nodiscard]] const Vec3& Ambient() const noexcept { return m_ambient; }

    // The active directional shadow caster (phase 5.1). Set during light extraction.
    void SetDirectionalShadow(const DirectionalShadow& s) noexcept { m_shadow = s; }
    [[nodiscard]] const DirectionalShadow& DirectionalShadowData() const noexcept { return m_shadow; }

    // Reset for a new frame: drop the item + light lists, rewind the (internal) arena.
    void Reset() noexcept { m_items.Clear(); m_lights.Clear(); m_localCasters.Clear(); m_ambient = Vec3{ 0.03f, 0.03f, 0.03f }; m_shadow = {}; m_arena.Reset(); }

    [[nodiscard]] Span<RenderData* const> Items() const noexcept {
        return Span<RenderData* const>{ m_items.Data(), m_items.Size() };
    }
    [[nodiscard]] Span<const GpuLight> Lights() const noexcept {
        return Span<const GpuLight>{ m_lights.Data(), m_lights.Size() };
    }
    [[nodiscard]] usize Size() const noexcept { return m_items.Size(); }
    [[nodiscard]] bool  IsEmpty() const noexcept { return m_items.IsEmpty(); }

private:
    FrameArena              m_arena;
    Array<RenderData*>      m_items;
    Array<GpuLight>         m_lights;
    Array<LocalShadowCaster> m_localCasters;             // spot/point shadow casters (phase 5.3)
    Vec3               m_ambient = Vec3{ 0.03f, 0.03f, 0.03f };   // default dim ambient
    DirectionalShadow  m_shadow;                                  // active directional shadow caster
};

// ---- radix sort ------------------------------------------------------------------------
//
// LSD radix sort of DrawItems by their 64-bit key, ascending — O(N), stable, 8 passes of
// 8 bits. `scratch` is a caller-owned ping-pong buffer (reused across frames to avoid
// per-frame allocation). After the call `items` is sorted; `scratch`'s contents are
// unspecified.
inline void RadixSortDrawItems(Array<DrawItem>& items, Array<DrawItem>& scratch) {
    const usize n = items.Size();
    if (n < 2) { return; }
    scratch.Resize(n);

    Array<DrawItem>* src = &items;
    Array<DrawItem>* dst = &scratch;
    for (u32 shift = 0; shift < 64; shift += 8) {
        usize counts[256] = {};
        for (usize i = 0; i < n; ++i) { ++counts[((*src)[i].key >> shift) & 0xFFu]; }
        usize total = 0;
        for (u32 b = 0; b < 256; ++b) { const usize c = counts[b]; counts[b] = total; total += c; }
        for (usize i = 0; i < n; ++i) {
            const u8 bucket = static_cast<u8>(((*src)[i].key >> shift) & 0xFFu);
            (*dst)[counts[bucket]++] = (*src)[i];
        }
        Array<DrawItem>* tmp = src; src = dst; dst = tmp;
    }
    // 8 passes (even) → result ends back in `items`; nothing to copy.
}

} // namespace raptor::render
