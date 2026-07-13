// Draconic::EditorApp - :editor_icons partition.
//
// Hand-authored editor icon set: inline SVG strings materialized ONCE into shared
// ui::SVGDrawable instances (the VG/SVG stack renders them crisp at any size). Ported from
// Sedulous.Editor/EditorIcons.bf - same glyphs, adapted to this engine's asset types. Inline
// strings are deliberate at this stage: editor chrome versioned with the code, no VFS/pipeline
// coupling, works on a fresh checkout; external icon files can come with full themes later
// (the drawables don't care where the string came from).
//
// Lifetime: the application calls Initialize() at startup and Shutdown() at teardown (explicit,
// deterministic - no static-destruction-order games with allocators). Access via
// EditorIcons::Get(); drawables are null before Initialize.

module;
#include "Core/Prelude.h"

export module draconic.editor.app:editor_icons;

import draconic.core;
import draconic.ui;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;

    class EditorIcons
    {
    public:
        [[nodiscard]] static EditorIcons& Get()
        {
            static EditorIcons instance;
            return instance;
        }

        // === Viewport toolbar ===
        RefPtr<ui::SVGDrawable> translate;
        RefPtr<ui::SVGDrawable> rotate;
        RefPtr<ui::SVGDrawable> scale;
        RefPtr<ui::SVGDrawable> worldSpace;
        RefPtr<ui::SVGDrawable> localSpace;
        RefPtr<ui::SVGDrawable> grid;

        // === Asset types (browser rows/tiles + picker) ===
        RefPtr<ui::SVGDrawable> scene;
        RefPtr<ui::SVGDrawable> prefab;
        RefPtr<ui::SVGDrawable> mesh;
        RefPtr<ui::SVGDrawable> skinnedMesh;
        RefPtr<ui::SVGDrawable> material;
        RefPtr<ui::SVGDrawable> texture;
        RefPtr<ui::SVGDrawable> particleFx;
        RefPtr<ui::SVGDrawable> animation;
        RefPtr<ui::SVGDrawable> animGraph;
        RefPtr<ui::SVGDrawable> skeleton;
        RefPtr<ui::SVGDrawable> folder;
        RefPtr<ui::SVGDrawable> unknown;

        void Initialize()
        {
            if (m_initialized) { return; }
            m_initialized = true;
            translate   = ui::SVGDrawable::FromString(kTranslate);
            rotate      = ui::SVGDrawable::FromString(kRotate);
            scale       = ui::SVGDrawable::FromString(kScale);
            worldSpace  = ui::SVGDrawable::FromString(kWorldSpace);
            localSpace  = ui::SVGDrawable::FromString(kLocalSpace);
            grid        = ui::SVGDrawable::FromString(kGrid);
            scene       = ui::SVGDrawable::FromString(kScene);
            prefab      = ui::SVGDrawable::FromString(kPrefab);
            mesh        = ui::SVGDrawable::FromString(kMesh);
            skinnedMesh = ui::SVGDrawable::FromString(kSkinnedMesh);
            material    = ui::SVGDrawable::FromString(kMaterial);
            texture     = ui::SVGDrawable::FromString(kTexture);
            particleFx  = ui::SVGDrawable::FromString(kParticleFx);
            animation   = ui::SVGDrawable::FromString(kAnimation);
            animGraph   = ui::SVGDrawable::FromString(kAnimGraph);
            skeleton    = ui::SVGDrawable::FromString(kSkeleton);
            folder      = ui::SVGDrawable::FromString(kFolder);
            unknown     = ui::SVGDrawable::FromString(kUnknown);
        }

        void Shutdown()
        {
            translate = nullptr; rotate = nullptr; scale = nullptr;
            worldSpace = nullptr; localSpace = nullptr; grid = nullptr;
            scene = nullptr; prefab = nullptr; mesh = nullptr; skinnedMesh = nullptr;
            material = nullptr; texture = nullptr; particleFx = nullptr; animation = nullptr;
            animGraph = nullptr; skeleton = nullptr; folder = nullptr; unknown = nullptr;
            m_initialized = false;
        }

        /// The icon for a content-DB instance TYPE NAME ("StaticMeshAsset", ...). Never null
        /// after Initialize - unmatched types get the generic document glyph.
        [[nodiscard]] ui::SVGDrawable* ForAssetType(StringView typeName) const
        {
            if (typeName == u8"SceneDocument")       { return scene.Get(); }
            if (typeName == u8"ModelManifestAsset")  { return prefab.Get(); }
            if (typeName == u8"StaticMeshAsset")     { return mesh.Get(); }
            if (typeName == u8"SkinnedMeshAsset")    { return skinnedMesh.Get(); }
            if (typeName == u8"MaterialAsset")       { return material.Get(); }
            if (typeName == u8"TextureAsset")        { return texture.Get(); }
            if (typeName == u8"ImageAsset")          { return texture.Get(); }
            if (typeName == u8"ParticleEffectAsset") { return particleFx.Get(); }
            if (typeName == u8"AnimationClipAsset")  { return animation.Get(); }
            if (typeName == u8"AnimationGraphAsset") { return animGraph.Get(); }
            if (typeName == u8"SkeletonAsset")       { return skeleton.Get(); }
            return unknown.Get();
        }

    private:
        bool m_initialized = false;

        // === Glyphs (24x24 viewBox, single light-gray fill - tintable for future themes) ===

        // Translate gizmo - four arrows pointing outward from center.
        static constexpr StringView kTranslate = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 2l3 3h-2v4h-2V5H9l3-3z" fill="#E0E0E0"/>
  <path d="M12 22l-3-3h2v-4h2v4h2l-3 3z" fill="#E0E0E0"/>
  <path d="M2 12l3-3v2h4v2H5v2l-3-3z" fill="#E0E0E0"/>
  <path d="M22 12l-3 3v-2h-4v-2h4V9l3 3z" fill="#E0E0E0"/>
</svg>)svg";

        // Rotate gizmo - circular arrow.
        static constexpr StringView kRotate = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 4c4.42 0 8 3.58 8 8h-2.5c0-3.04-2.46-5.5-5.5-5.5S6.5 8.96 6.5 12s2.46 5.5 5.5 5.5c1.52 0 2.9-.62 3.89-1.61l1.77 1.77A7.96 7.96 0 0112 20c-4.42 0-8-3.58-8-8s3.58-8 8-8z" fill="#E0E0E0"/>
  <path d="M20 12l3-3v6l-3-3z" fill="#E0E0E0"/>
</svg>)svg";

        // Scale gizmo - diagonal arrow with corner squares.
        static constexpr StringView kScale = u8R"svg(<svg viewBox="0 0 24 24">
  <rect x="3" y="3" width="5" height="5" fill="#E0E0E0"/>
  <path d="M8 8l10 10" stroke="#E0E0E0" stroke-width="2" stroke-linecap="round"/>
  <rect x="16" y="16" width="5" height="5" fill="#E0E0E0"/>
  <path d="M14 20h6v-6" fill="none" stroke="#E0E0E0" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)svg";

        // World space - globe.
        static constexpr StringView kWorldSpace = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="12" cy="12" r="9" fill="none" stroke="#E0E0E0" stroke-width="1.5"/>
  <ellipse cx="12" cy="12" rx="4" ry="9" fill="none" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="3" y1="12" x2="21" y2="12" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="12" y1="3" x2="12" y2="21" stroke="#E0E0E0" stroke-width="1"/>
</svg>)svg";

        // Local space - cube.
        static constexpr StringView kLocalSpace = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 2L4 7v10l8 5 8-5V7l-8-5z" fill="none" stroke="#E0E0E0" stroke-width="1.5" stroke-linejoin="round"/>
  <path d="M12 22V12M4 7l8 5 8-5" fill="none" stroke="#E0E0E0" stroke-width="1" stroke-linejoin="round"/>
</svg>)svg";

        // Debug grid - 4x4 grid pattern.
        static constexpr StringView kGrid = u8R"svg(<svg viewBox="0 0 24 24">
  <rect x="3" y="3" width="18" height="18" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <line x1="9"  y1="3" x2="9"  y2="21" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="15" y1="3" x2="15" y2="21" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="3" y1="9"  x2="21" y2="9"  stroke="#E0E0E0" stroke-width="1"/>
  <line x1="3" y1="15" x2="21" y2="15" stroke="#E0E0E0" stroke-width="1"/>
</svg>)svg";

        // Scene - linked nodes (a scene graph).
        static constexpr StringView kScene = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="12" cy="5" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="5" cy="18" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="19" cy="18" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <path d="M11 7l-5 9M13 7l5 9M7 18h10" fill="none" stroke="#E0E0E0" stroke-width="1.2"/>
</svg>)svg";

        // Prefab/model manifest - cube outline with an inset cube (template).
        static constexpr StringView kPrefab = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 2L4 6v12l8 4 8-4V6l-8-4z" fill="none" stroke="#E0E0E0" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M12 22V11M4 6l8 5 8-5" fill="none" stroke="#E0E0E0" stroke-width="1"/>
  <rect x="9" y="13" width="6" height="6" fill="none" stroke="#E0E0E0" stroke-width="1" stroke-linejoin="round"/>
</svg>)svg";

        // Mesh - wireframe pyramid.
        static constexpr StringView kMesh = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 3L3 20h18L12 3z" fill="none" stroke="#E0E0E0" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M12 3v17M3 20l9-6 9 6" fill="none" stroke="#E0E0E0" stroke-width="1"/>
</svg>)svg";

        // Skinned mesh - pyramid + bone overlay.
        static constexpr StringView kSkinnedMesh = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M12 3L3 20h18L12 3z" fill="none" stroke="#E0E0E0" stroke-width="1.2" stroke-linejoin="round"/>
  <path d="M12 3v17M3 20l9-6 9 6" fill="none" stroke="#E0E0E0" stroke-width="0.8"/>
  <circle cx="12" cy="9" r="1.6" fill="#E0E0E0"/>
  <circle cx="12" cy="17" r="1.6" fill="#E0E0E0"/>
  <line x1="12" y1="10.5" x2="12" y2="15.5" stroke="#E0E0E0" stroke-width="1.4"/>
</svg>)svg";

        // Material - shaded sphere with highlight.
        static constexpr StringView kMaterial = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="12" cy="12" r="9" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <path d="M3 12a9 9 0 0118 0" fill="none" stroke="#E0E0E0" stroke-width="0.9"/>
  <circle cx="9" cy="9" r="1.6" fill="#E0E0E0"/>
</svg>)svg";

        // Texture/image - image frame with mountain glyph.
        static constexpr StringView kTexture = u8R"svg(<svg viewBox="0 0 24 24">
  <rect x="3" y="4" width="18" height="16" fill="none" stroke="#E0E0E0" stroke-width="1.4" rx="1"/>
  <circle cx="8" cy="9" r="1.5" fill="#E0E0E0"/>
  <path d="M3 17l5-5 4 4 3-3 6 6" fill="none" stroke="#E0E0E0" stroke-width="1.2" stroke-linejoin="round"/>
</svg>)svg";

        // Particle effect - burst pattern.
        static constexpr StringView kParticleFx = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="12" cy="12" r="2" fill="#E0E0E0"/>
  <circle cx="4" cy="6" r="1.2" fill="#E0E0E0"/>
  <circle cx="20" cy="6" r="1.2" fill="#E0E0E0"/>
  <circle cx="3" cy="14" r="1" fill="#E0E0E0"/>
  <circle cx="21" cy="15" r="1" fill="#E0E0E0"/>
  <circle cx="7" cy="20" r="1" fill="#E0E0E0"/>
  <circle cx="17" cy="20" r="1" fill="#E0E0E0"/>
  <circle cx="12" cy="3" r="1.2" fill="#E0E0E0"/>
</svg>)svg";

        // Animation clip - filmstrip.
        static constexpr StringView kAnimation = u8R"svg(<svg viewBox="0 0 24 24">
  <rect x="3" y="5" width="18" height="14" fill="none" stroke="#E0E0E0" stroke-width="1.4" rx="1"/>
  <line x1="3" y1="9" x2="21" y2="9" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="3" y1="15" x2="21" y2="15" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="9" y1="5" x2="9" y2="19" stroke="#E0E0E0" stroke-width="1"/>
  <line x1="15" y1="5" x2="15" y2="19" stroke="#E0E0E0" stroke-width="1"/>
</svg>)svg";

        // Animation graph - connected state nodes.
        static constexpr StringView kAnimGraph = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="6" cy="6" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="18" cy="6" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="6" cy="18" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="18" cy="18" r="2.2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <path d="M8 6h8M6 8v8M18 8v8M8 18h8M8 8l8 8" fill="none" stroke="#E0E0E0" stroke-width="1.2"/>
</svg>)svg";

        // Skeleton - articulated stick figure.
        static constexpr StringView kSkeleton = u8R"svg(<svg viewBox="0 0 24 24">
  <circle cx="12" cy="5" r="2" fill="none" stroke="#E0E0E0" stroke-width="1.4"/>
  <line x1="12" y1="7" x2="12" y2="14" stroke="#E0E0E0" stroke-width="1.4"/>
  <line x1="7" y1="10" x2="17" y2="10" stroke="#E0E0E0" stroke-width="1.4"/>
  <line x1="12" y1="14" x2="8" y2="20" stroke="#E0E0E0" stroke-width="1.4"/>
  <line x1="12" y1="14" x2="16" y2="20" stroke="#E0E0E0" stroke-width="1.4"/>
  <circle cx="12" cy="14" r="1.2" fill="#E0E0E0"/>
  <circle cx="7" cy="10" r="1" fill="#E0E0E0"/>
  <circle cx="17" cy="10" r="1" fill="#E0E0E0"/>
</svg>)svg";

        // Folder - classic folder glyph.
        static constexpr StringView kFolder = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M3 7a1 1 0 011-1h5l2 2h9a1 1 0 011 1v9a1 1 0 01-1 1H4a1 1 0 01-1-1V7z" fill="none" stroke="#E0E0E0" stroke-width="1.4" stroke-linejoin="round"/>
</svg>)svg";

        // Unknown - generic document with a folded corner.
        static constexpr StringView kUnknown = u8R"svg(<svg viewBox="0 0 24 24">
  <path d="M6 3h8l5 5v12a1 1 0 01-1 1H6a1 1 0 01-1-1V4a1 1 0 011-1z" fill="none" stroke="#E0E0E0" stroke-width="1.4" stroke-linejoin="round"/>
  <path d="M14 3v5h5" fill="none" stroke="#E0E0E0" stroke-width="1.2"/>
</svg>)svg";
    };
}
