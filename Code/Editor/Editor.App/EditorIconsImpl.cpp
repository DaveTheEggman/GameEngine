// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :editor_icons partition.
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

module editor.app;

import foundation.core;
import foundation.ui;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
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
        translate = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kTranslate);
        rotate = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kRotate);
        scale = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kScale);
        worldSpace = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kWorldSpace);
        localSpace = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kLocalSpace);
        grid = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kGrid);
        scene = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kScene);
        prefab = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kPrefab);
        mesh = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kMesh);
        skinnedMesh = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kSkinnedMesh);
        material = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kMaterial);
        texture = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kTexture);
        particleFx = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kParticleFx);
        animation = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kAnimation);
        animGraph = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kAnimGraph);
        skeleton = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kSkeleton);
        folder = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kFolder);
        unknown = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kUnknown);
        close = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kClose);
        add = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kAdd);
        remove = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kRemove);
        moveUp = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kMoveUp);
        moveDown = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kMoveDown);
        copy = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kCopy);
        edit = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kEdit);
        brushRaise = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kBrushRaise);
        brushLower = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kBrushLower);
        brushSmooth = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kBrushSmooth);
        brushFlatten = ui::BakedSVGDrawable::FromString(foundation::core::DefaultAllocator(), kBrushFlatten);
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
        add = nullptr;
        remove = nullptr;
        moveUp = nullptr;
        moveDown = nullptr;
        copy = nullptr;
        edit = nullptr;
        brushRaise = nullptr;
        brushLower = nullptr;
        brushSmooth = nullptr;
        brushFlatten = nullptr;
        m_initialized = false;
    }

    Array<ui::BakedSVGDrawable*> EditorIcons::Bakeable() const
{
    Array<ui::BakedSVGDrawable*> icons;
    const RefPtr<ui::BakedSVGDrawable>* all[] = {
        &translate, &rotate,     &scale,    &worldSpace, &localSpace, &grid,   &scene,
        &prefab,    &mesh,       &skinnedMesh, &material, &texture,   &particleFx,
        &animation, &animGraph,  &skeleton, &folder,     &unknown,    &close,
        &add,       &remove,     &moveUp,   &moveDown,   &copy,       &edit,
        &brushRaise, &brushLower, &brushSmooth, &brushFlatten};
    for (const RefPtr<ui::BakedSVGDrawable>* icon : all)
    {
        if (icon->Get() != nullptr)
        {
            icons.PushBack(icon->Get());
        }
    }
    return icons;
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
