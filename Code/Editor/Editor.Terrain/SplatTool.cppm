// Editor::Terrain - :splat partition.
//
// TerrainSplatTool: the scene-viewport terrain layer-weight brush (editor.viewporttools), the second
// terrain tool after Sculpt. It ray-picks the terrain heightfield under the cursor, maps the hit to
// the splatmap's 0..1 footprint UV, and paints the SELECTED layer channel into the SHARED runtime
// Splatmap (lerp-to-one-hot - painting another layer erases this one), records ONE region-delta undo
// command per stroke, and on Save writes the painted RGBA8 back to its SOURCE asset's pixel sidecar.
//
// GCC module hygiene: the heavy engine.terrain / resource imports live in SplatToolImpl.cpp; this
// interface stays on the light modules only.

module;
#include "Core/Prelude.h"

export module editor.terrain:splat;

import foundation.core;
import foundation.scene;            // EntityHandle
import foundation.render;           // debug::DebugDraw (the Draw override signature)
import foundation.terrain.resource; // Splatmap (RefPtr kept alive across a stroke), SplatRegion
import editor.core;                 // EditorCommandStack, IAssetEditSink
import editor.viewporttools;        // IViewportTool, ViewportToolInput

using namespace foundation::core;

export namespace editor
{
    /// The terrain splat (layer-weight) brush. One per scene page (created by the terrain provider);
    /// borrows the scene, command stack and asset-edit sink from the host context.
    class TerrainSplatTool final : public IViewportTool
    {
    public:
        TerrainSplatTool(foundation::scene::Scene& scene, EditorCommandStack& commands,
                         IAssetEditSink* assetEdits)
            : m_scene(&scene), m_commands(&commands), m_assetEdits(assetEdits)
        {
        }

        [[nodiscard]] StringView Id() const override { return u8"terrain.splat"; }
        [[nodiscard]] StringView DisplayName() const override { return u8"Paint Splat"; }

        /// Relevant only when the scene has a terrain whose splatmap (and heightfield, for the pick)
        /// resolves.
        [[nodiscard]] bool IsAvailable() const override;

        [[nodiscard]] bool Update(const ViewportToolInput& input) override;
        void Draw(foundation::render::debug::DebugDraw& drawList) override;
        void OnDeactivate() override;
        [[nodiscard]] StringView StatusText() const override { return m_status.AsView(); }

        // ---- brush parameters (the future tool panel + the page's layer list drive these) ----
        void SetLayer(u32 layer) noexcept { m_layer = layer & 3u; }
        [[nodiscard]] u32 Layer() const noexcept { return m_layer; }
        void SetRadius(f32 r) noexcept { m_radius = Clamp(r, kMinRadius, kMaxRadius); }
        [[nodiscard]] f32 Radius() const noexcept { return m_radius; }
        void SetStrength(f32 s) noexcept { m_strength = Clamp(s, 0.0f, 1.0f); }
        [[nodiscard]] f32 Strength() const noexcept { return m_strength; }

    private:
        static constexpr f32 kMinRadius = 0.5f;
        static constexpr f32 kMaxRadius = 128.0f;

        struct Pick
        {
            foundation::terrain::Splatmap* splat = nullptr;
            Guid splatmapId;      // TerrainResource.splatmap.id (the SOURCE asset guid; may be nil)
            f32 uvX = 0.0f;       // hit in the splatmap's 0..1 footprint UV
            f32 uvY = 0.0f;
            f32 worldSizeX = 1.0f; // footprint width (world radius -> uv radius)
            Float3 worldHit{};
            Float3 worldNormal{0.0f, 1.0f, 0.0f};
            bool valid = false;
        };
        [[nodiscard]] Pick ResolvePick(const ViewportToolInput& input) const;

        void BeginStroke(const Pick& pick);
        void ApplyDab(const Pick& pick, f32 deltaSeconds);
        void EndStroke();
        void UpdateStatus();

        foundation::scene::Scene* m_scene;      // borrowed (page owns it)
        EditorCommandStack* m_commands;         // borrowed
        IAssetEditSink* m_assetEdits = nullptr; // borrowed; null = no persistence (tests)

        u32 m_layer = 0;       // which channel (0..3) the brush paints
        f32 m_radius = 6.0f;   // world units
        f32 m_strength = 1.0f; // 0..1 weight added per second at the brush centre

        // Hover (cursor overlay).
        bool m_hasHover = false;
        Float3 m_hoverWorld{};
        Float3 m_hoverNormal{0.0f, 1.0f, 0.0f};

        // Stroke state (one command per press..release). m_strokeSplat keeps the raster alive for
        // the whole stroke; m_before is a full pixel snapshot (the union region is unknown until
        // release, so the command slices before/after out of it then).
        bool m_stroking = false;
        RefPtr<foundation::terrain::Splatmap> m_strokeSplat;
        Guid m_strokeSplatmapId;
        Array<u8> m_before;
        foundation::terrain::SplatRegion m_region;

        String m_status;
    };
}
