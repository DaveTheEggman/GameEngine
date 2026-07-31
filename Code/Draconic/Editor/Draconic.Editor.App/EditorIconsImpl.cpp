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
#include "Draconic.Core/Prelude.h"

module draconic.editor.app;

import draconic.core;
import draconic.ui;

using namespace draconic::core;

namespace draconic::editor::app
{
    EditorIcons& EditorIcons::Get()
    {
        static EditorIcons instance;
        return instance;
    }

    void EditorIcons::Initialize()
    {
        if (m_initialized)
        {
            return;
        }
        m_initialized = true;
        translate = ui::SVGDrawable::FromString(kTranslate);
        rotate = ui::SVGDrawable::FromString(kRotate);
        scale = ui::SVGDrawable::FromString(kScale);
        worldSpace = ui::SVGDrawable::FromString(kWorldSpace);
        localSpace = ui::SVGDrawable::FromString(kLocalSpace);
        grid = ui::SVGDrawable::FromString(kGrid);
        scene = ui::SVGDrawable::FromString(kScene);
        prefab = ui::SVGDrawable::FromString(kPrefab);
        mesh = ui::SVGDrawable::FromString(kMesh);
        skinnedMesh = ui::SVGDrawable::FromString(kSkinnedMesh);
        material = ui::SVGDrawable::FromString(kMaterial);
        texture = ui::SVGDrawable::FromString(kTexture);
        particleFx = ui::SVGDrawable::FromString(kParticleFx);
        animation = ui::SVGDrawable::FromString(kAnimation);
        animGraph = ui::SVGDrawable::FromString(kAnimGraph);
        skeleton = ui::SVGDrawable::FromString(kSkeleton);
        folder = ui::SVGDrawable::FromString(kFolder);
        unknown = ui::SVGDrawable::FromString(kUnknown);
    }

    void EditorIcons::Shutdown()
    {
        translate = nullptr;
        rotate = nullptr;
        scale = nullptr;
        worldSpace = nullptr;
        localSpace = nullptr;
        grid = nullptr;
        scene = nullptr;
        prefab = nullptr;
        mesh = nullptr;
        skinnedMesh = nullptr;
        material = nullptr;
        texture = nullptr;
        particleFx = nullptr;
        animation = nullptr;
        animGraph = nullptr;
        skeleton = nullptr;
        folder = nullptr;
        unknown = nullptr;
        m_initialized = false;
    }

    ui::SVGDrawable* EditorIcons::ForAssetType(StringView typeName) const
    {
        if (typeName == u8"SceneDocument")
        {
            return scene.Get();
        }
        if (typeName == u8"ModelManifestAsset")
        {
            return prefab.Get();
        }
        if (typeName == u8"StaticMeshAsset")
        {
            return mesh.Get();
        }
        if (typeName == u8"SkinnedMeshAsset")
        {
            return skinnedMesh.Get();
        }
        if (typeName == u8"MaterialAsset")
        {
            return material.Get();
        }
        if (typeName == u8"TextureAsset")
        {
            return texture.Get();
        }
        if (typeName == u8"ImageAsset")
        {
            return texture.Get();
        }
        if (typeName == u8"ParticleEffectAsset")
        {
            return particleFx.Get();
        }
        if (typeName == u8"AnimationClipAsset")
        {
            return animation.Get();
        }
        if (typeName == u8"AnimationGraphAsset")
        {
            return animGraph.Get();
        }
        if (typeName == u8"SkeletonAsset")
        {
            return skeleton.Get();
        }
        return unknown.Get();
    }
}
