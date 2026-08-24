// Editor::Terrain - :splat implementation (TerrainSplatTool). Also extends the terrain viewport-tool
// provider (SculptImpl.cpp) to contribute this second tool.
//
// The heavy engine/resource imports the interface partition keeps out (GCC hygiene) live here: the
// terrain component + manager (to find the terrain + its shared splatmap) and the splatmap source
// (to serialize the painted RGBA8 back on save).

module;
#include "Core/Prelude.h"

module editor.terrain;

import foundation.core;
import foundation.scene;
import foundation.render;            // debug::DebugDraw
import foundation.shell;            // IKeyboard (layer hotkeys)
import foundation.content;          // ContentDatabase, Instance (the persist closure)
import foundation.resource;         // Ref<>
import foundation.heightfield;      // Heightfield (ray-pick for the UV mapping)
import foundation.terrain.resource; // Splatmap, PaintWeight, SplatmapSource
import terrain.pipeline;            // SplatmapAsset (the SOURCE envelope the persist rewrites)
import engine.terrain;              // TerrainComponent + TerrainComponentManager
import editor.core;
import editor.viewporttools;

using namespace foundation::core;

namespace editor
{
    namespace scene = foundation::scene;
    namespace terrain = foundation::terrain;
    namespace hf = foundation::heightfield;
    namespace render = foundation::render;
    namespace content = foundation::content;

    namespace
    {
        [[nodiscard]] engine::terrain::TerrainComponentManager* SplatTerrainManager(scene::Scene& scene)
        {
            scene::ComponentManagerBase* base =
                scene.FindManagerByComponentType(TypeOf<engine::terrain::TerrainComponent>());
            return static_cast<engine::terrain::TerrainComponentManager*>(base);
        }

        // The region-delta stroke command over RGBA8 pixels: replays the touched pixel RECTANGLE
        // between its before / after blocks. One per stroke; never merges. Keeps the raster alive.
        class SplatStrokeCommand final : public IEditorCommand
        {
        public:
            SplatStrokeCommand(RefPtr<terrain::Splatmap> splat, terrain::SplatRegion region,
                               Array<u8> before, Array<u8> after)
                : m_splat(Move(splat)), m_region(region), m_before(Move(before)),
                  m_after(Move(after))
            {
            }

            [[nodiscard]] bool Execute() override
            {
                Write(m_after); // live raster is already AFTER on push (no-op), replays on redo
                return !m_region.IsEmpty();
            }
            void Undo() override { Write(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"terrain.splat.stroke"; }

        private:
            void Write(const Array<u8>& block)
            {
                if (m_splat.Get() == nullptr || m_region.IsEmpty())
                {
                    return;
                }
                const i32 w = m_region.Width();
                Span<u8> px = m_splat->Pixels();
                const i32 rasterW = m_splat->Width();
                for (i32 y = 0; y < m_region.Height(); ++y)
                {
                    for (i32 x = 0; x < w; ++x)
                    {
                        const usize dst = (static_cast<usize>(m_region.minY + y) *
                                               static_cast<usize>(rasterW) +
                                           static_cast<usize>(m_region.minX + x)) *
                                          4u;
                        const usize src =
                            (static_cast<usize>(y) * static_cast<usize>(w) + static_cast<usize>(x)) *
                            4u;
                        for (u32 k = 0; k < 4; ++k)
                        {
                            px[dst + k] = block[src + k];
                        }
                    }
                }
                m_splat->BumpVersion();
            }

            RefPtr<terrain::Splatmap> m_splat;
            terrain::SplatRegion m_region;
            Array<u8> m_before;
            Array<u8> m_after;
        };

        // Slice the inclusive pixel region (RGBA8) out of a full-raster pixel array.
        [[nodiscard]] Array<u8> SliceRegion(Span<const u8> full, i32 rasterW,
                                            const terrain::SplatRegion& r)
        {
            Array<u8> out;
            if (r.IsEmpty())
            {
                return out;
            }
            const i32 w = r.Width();
            const i32 h = r.Height();
            out.Resize(static_cast<usize>(w) * static_cast<usize>(h) * 4u);
            for (i32 y = 0; y < h; ++y)
            {
                for (i32 x = 0; x < w; ++x)
                {
                    const usize src = (static_cast<usize>(r.minY + y) * static_cast<usize>(rasterW) +
                                       static_cast<usize>(r.minX + x)) *
                                      4u;
                    const usize dst =
                        (static_cast<usize>(y) * static_cast<usize>(w) + static_cast<usize>(x)) * 4u;
                    for (u32 k = 0; k < 4; ++k)
                    {
                        out[dst + k] = full[src + k];
                    }
                }
            }
            return out;
        }
    }

    bool TerrainSplatTool::IsAvailable() const
    {
        engine::terrain::TerrainComponentManager* mgr = SplatTerrainManager(*m_scene);
        if (mgr == nullptr)
        {
            return false;
        }
        bool any = false;
        mgr->ForEach(
            [&](engine::terrain::TerrainComponent& c, scene::EntityHandle)
            {
                if (any)
                {
                    return;
                }
                terrain::TerrainResource* res = c.terrain.Get();
                if (res != nullptr && res->splatmap.Get() != nullptr &&
                    !res->splatmap.Get()->IsEmpty() && res->heightfield.Get() != nullptr &&
                    !res->heightfield.Get()->IsEmpty())
                {
                    any = true;
                }
            });
        return any;
    }

    TerrainSplatTool::Pick TerrainSplatTool::ResolvePick(const ViewportToolInput& input) const
    {
        Pick best;
        engine::terrain::TerrainComponentManager* mgr = SplatTerrainManager(*m_scene);
        if (mgr == nullptr)
        {
            return best;
        }
        f32 bestDist = kFloatMax;
        mgr->ForEach(
            [&](engine::terrain::TerrainComponent& c, scene::EntityHandle owner)
            {
                terrain::TerrainResource* res = c.terrain.Get();
                if (res == nullptr)
                {
                    return;
                }
                terrain::Splatmap* splat = res->splatmap.Get();
                hf::Heightfield* grid = res->heightfield.Get();
                if (splat == nullptr || splat->IsEmpty() || grid == nullptr || grid->IsEmpty())
                {
                    return;
                }
                const Float4x4 world = m_scene->GetWorldMatrix(owner);
                const Float4x4 inv = Inverse(world);
                const Float3 localOrigin = TransformPoint(input.ray.origin, inv);
                const Float3 localDir = TransformDirection(input.ray.direction, inv);
                f32 t = 0.0f;
                if (!grid->QueryRay(localOrigin, localDir, t))
                {
                    return;
                }
                const Float3 localHit = localOrigin + Normalized(localDir) * t;
                const Float3 worldHit = TransformPoint(localHit, world);
                const f32 dist = Length(worldHit - input.ray.origin);
                if (dist >= bestDist)
                {
                    return;
                }
                // Map the local XZ hit to the 0..1 footprint UV the splatmap covers (the same
                // mapping the shader's splatUV uses: centred on the local origin).
                const Float2 ws = grid->WorldSize();
                const f32 uvX = localHit.x / (ws.x != 0.0f ? ws.x : 1.0f) + 0.5f;
                const f32 uvY = localHit.z / (ws.y != 0.0f ? ws.y : 1.0f) + 0.5f;
                bestDist = dist;
                best.splat = splat;
                best.splatmapId = res->splatmap.id;
                best.uvX = uvX;
                best.uvY = uvY;
                best.worldSizeX = ws.x != 0.0f ? ws.x : 1.0f;
                best.worldHit = worldHit;
                best.worldNormal = Normalized(TransformDirection(grid->GetNormalAt(localHit.x, localHit.z), world));
                best.valid = true;
            });
        return best;
    }

    bool TerrainSplatTool::Update(const ViewportToolInput& input)
    {
        m_hasHover = false;

        // Layer hotkeys 1..4 -> layers 0..3 (a pressed key sets the layer; the panel mirrors it).
        if (foundation::shell::IKeyboard* kb = input.keyboard; kb != nullptr && input.pointerValid)
        {
            using foundation::shell::KeyCode;
            if (kb->IsKeyPressed(KeyCode::Num1)) m_layer = 0;
            if (kb->IsKeyPressed(KeyCode::Num2)) m_layer = 1;
            if (kb->IsKeyPressed(KeyCode::Num3)) m_layer = 2;
            if (kb->IsKeyPressed(KeyCode::Num4)) m_layer = 3;
        }

        if (input.pointerOver && input.wheelDelta != 0.0f)
        {
            SetRadius(m_radius * (1.0f + 0.12f * input.wheelDelta));
        }

        const Pick pick = ResolvePick(input);
        if (pick.valid)
        {
            m_hasHover = true;
            m_hoverWorld = pick.worldHit;
            m_hoverNormal = pick.worldNormal;
        }

        bool consumed = m_stroking;

        if (!input.editingLocked) // Simulate: the splatmap feeds the live material - no edits
        {
            if (!m_stroking && pick.valid && input.pointerOver && input.leftPressed)
            {
                BeginStroke(pick);
                consumed = true;
            }
            else if (m_stroking && input.leftDown && pick.valid && pick.splat == m_strokeSplat.Get())
            {
                ApplyDab(pick, input.deltaSeconds);
                consumed = true;
            }

            if (m_stroking && (input.leftReleased || !input.pointerValid))
            {
                EndStroke();
            }
        }
        else if (m_stroking)
        {
            EndStroke();
        }

        UpdateStatus();
        return consumed;
    }

    void TerrainSplatTool::BeginStroke(const Pick& pick)
    {
        m_stroking = true;
        m_strokeSplat = RefPtr<terrain::Splatmap>(pick.splat); // keep the raster alive for the stroke
        m_strokeSplatmapId = pick.splatmapId;
        const Span<const u8> px = pick.splat->Pixels();
        m_before.Resize(px.Size());
        if (!px.IsEmpty())
        {
            MemCopy(m_before.Data(), px.Data(), px.Size());
        }
        m_region = terrain::SplatRegion{};
        ApplyDab(pick, 0.0f); // an instant click still deposits one dab (dt=0 -> a minimum step)
    }

    void TerrainSplatTool::ApplyDab(const Pick& pick, f32 deltaSeconds)
    {
        if (m_strokeSplat.Get() == nullptr)
        {
            return;
        }
        const f32 step = m_strength * (deltaSeconds > 0.0f ? deltaSeconds : (1.0f / 60.0f));
        const f32 uvRadius = m_radius / pick.worldSizeX; // world disc -> UV (square footprint assumed)
        const terrain::SplatRegion r =
            terrain::PaintWeight(*m_strokeSplat, pick.uvX, pick.uvY, uvRadius, m_layer,
                                 Clamp(step, 0.0f, 1.0f));
        if (!r.IsEmpty())
        {
            m_region.Add(r.minX, r.minY);
            m_region.Add(r.maxX, r.maxY);
        }
    }

    void TerrainSplatTool::EndStroke()
    {
        const bool hadRegion = m_stroking && m_strokeSplat.Get() != nullptr && !m_region.IsEmpty();
        if (hadRegion)
        {
            const i32 rasterW = m_strokeSplat->Width();
            Array<u8> before =
                SliceRegion(Span<const u8>{m_before.Data(), m_before.Size()}, rasterW, m_region);
            Array<u8> after = SliceRegion(m_strokeSplat->Pixels(), rasterW, m_region);
            m_commands->Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<SplatStrokeCommand>(m_strokeSplat, m_region, Move(before),
                                                           Move(after)),
                DefaultAllocator()));

            // Register the write-back-to-source persist closure (drained on Save). It writes ONLY
            // the source SplatmapAsset's "pixels" sidecar - the builder re-cooks from those pixels,
            // and the asset's width/height envelope is left intact.
            if (m_assetEdits != nullptr && !m_strokeSplatmapId.IsNil())
            {
                RefPtr<terrain::Splatmap> splat = m_strokeSplat;
                const Guid id = m_strokeSplatmapId;
                m_assetEdits->RegisterAssetEdit(
                    id,
                    [splat, id](content::ContentDatabase& db) -> Status
                    {
                        // Read-modify-write the SOURCE SplatmapAsset envelope: sync the raster
                        // dims and CLEAR fileName, so an IMPORTED (PNG-backed) splatmap converts
                        // to embedded on the first paint-save - otherwise the builder keeps
                        // cooking from the file and the paint silently reverts on re-cook (the
                        // editable-source convention; re-import explicitly resets by setting
                        // fileName again). Then write the painted pixels sidecar the embedded
                        // cook reads.
                        content::Instance* inst = db.GetInstance(id);
                        if (inst == nullptr || splat.Get() == nullptr)
                        {
                            return Status{ErrorCode::NotFound};
                        }
                        RefPtr<ISerializable> object = inst->ReadObject();
                        auto* asset = Cast<pipeline::SplatmapAsset>(object.Get());
                        if (asset == nullptr)
                        {
                            return Status{ErrorCode::InvalidArgument}; // not a splatmap asset
                        }
                        asset->fileName = {};
                        asset->width = splat->Width();
                        asset->height = splat->Height();
                        const Status wrote = inst->WriteObject(*asset);
                        if (!wrote.IsOk())
                        {
                            return wrote;
                        }
                        return inst->WriteData(terrain::kSplatStream,
                                               terrain::SplatmapSource::PixelBlob(*splat));
                    });
            }
        }

        m_stroking = false;
        m_strokeSplat = nullptr;
        m_before.Clear();
        m_region = terrain::SplatRegion{};
    }

    void TerrainSplatTool::OnDeactivate()
    {
        if (m_stroking)
        {
            EndStroke(); // gesture-end guarantee: never leave a half-open stroke on a tool switch
        }
        m_hasHover = false;
    }

    void TerrainSplatTool::Draw(render::debug::DebugDraw& drawList)
    {
        if (!m_hasHover)
        {
            return;
        }
        // Per-layer cursor tint (RGBA -> a rough R/G/B/white so the active layer reads at a glance).
        const Color tints[4] = {Color{1.0f, 0.35f, 0.35f, 1.0f}, Color{0.4f, 1.0f, 0.4f, 1.0f},
                                Color{0.4f, 0.6f, 1.0f, 1.0f}, Color{0.95f, 0.95f, 0.95f, 1.0f}};
        const Color ring = tints[m_layer & 3u];
        drawList.DrawCircleNormal(m_hoverWorld, m_radius, m_hoverNormal, ring, 40, true);
        drawList.DrawCircleNormal(m_hoverWorld, m_radius * 0.5f, m_hoverNormal,
                                  Color{ring.r, ring.g, ring.b, 0.5f}, 32, true);
    }

    void TerrainSplatTool::UpdateStatus()
    {
        m_status =
            Format(u8"Paint Splat [layer {}]  radius {}  (1-4 layer, wheel size)",
                   static_cast<i32>(m_layer), static_cast<i32>(m_radius + 0.5f));
    }
}
