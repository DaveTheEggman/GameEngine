// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The collision shape editor's labels, its outline mesh and its thumbnail generator, headless.
// Ported back from the Beef port's Editor.Physics.Tests (2026-09-20); the page-factory routing
// case is not here because the factory takes a UI host only a window can make.
#include <doctest/doctest.h>
#include <filesystem>
#include "Core/Prelude.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.scene;
import foundation.geometry;
import foundation.physics.resource;
import physics.pipeline;
import editor.core;
import editor.physics;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace geometry = foundation::geometry;
namespace physics = foundation::physics;
namespace content = foundation::content;

TEST_CASE("collision editor: the cook labels name the kind, and the outline becomes a lit triangle mesh")
{
    CHECK(editor::CollisionShapeEditorPage::CookLabel(pipeline::CollisionCookKind::ConvexHull) ==
          StringView(u8"Convex hull (dynamic)"));
    CHECK(editor::CollisionShapeEditorPage::CookLabel(pipeline::CollisionCookKind::TriangleMesh) ==
          StringView(u8"Triangle mesh (static)"));

    auto shapeRef = MakeRef<physics::CollisionShape>(DefaultAllocator());
    physics::CollisionShape& shape = *shapeRef;
    shape.outline.PushBack(Float3{0, 0, 0});
    shape.outline.PushBack(Float3{1, 0, 0});
    shape.outline.PushBack(Float3{0, 0, 1});
    shape.outline.PushBack(Float3{0, 1, 0});
    shape.outline.PushBack(Float3{1, 1, 0});
    shape.outline.PushBack(Float3{0, 1, 1});
    shape.outline.PushBack(Float3{5, 5, 5}); // a stray vertex past the last whole triangle: dropped
    auto meshRef = MakeRef<geometry::StaticMesh>(DefaultAllocator());
    geometry::StaticMesh& mesh = *meshRef;
    REQUIRE(editor::BuildCollisionOutlineMesh(shape, mesh));
    CHECK(mesh.VertexCount() == 6u);
    CHECK(mesh.IndexCount() == 6u);
    CHECK(mesh.subMeshes.Size() == 1u);
    CHECK(mesh.bounds.max.y == 1.0f);

    // Too few vertices for a triangle: nothing, and the mesh is left empty.
    auto flatRef = MakeRef<physics::CollisionShape>(DefaultAllocator());
    flatRef->outline.PushBack(Float3{0, 0, 0});
    flatRef->outline.PushBack(Float3{1, 0, 0});
    CHECK_FALSE(editor::BuildCollisionOutlineMesh(*flatRef, mesh));
    CHECK(mesh.VertexCount() == 0u);
}

TEST_CASE("collision editor: the thumbnail generator claims the asset type and shares the stage")
{
    UniquePtr<editor::ISceneThumbnailGenerator> generator = editor::CreateCollisionThumbnailGenerator();
    REQUIRE(generator);
    const Span<const StringView> names = generator->AssetTypeNames();
    REQUIRE(names.Size() == 1u);
    CHECK(names[0] == StringView(u8"CollisionShapeAsset"));
    CHECK_FALSE(generator->NeedsPrivateScene());
}

TEST_CASE("collision editor: staging without the render managers fails rather than crashes")
{
    // A resource manager over an empty scratch database: the stage never gets far enough to
    // bind through it, which is the point.
    const std::filesystem::path dir = "scratch_editor_physics_stage";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        foundation::vfs::NativeFileSystem mount(u8"scratch_editor_physics_stage", DefaultAllocator());
        content::ContentDatabase db(DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");
        foundation::resource::ResourceManager resources(DefaultAllocator(), db);

        UniquePtr<editor::ISceneThumbnailGenerator> generator =
            editor::CreateCollisionThumbnailGenerator();
        scene::Scene stage(DefaultAllocator(), u8"stage"); // no MeshComponentManager, no lights
        editor::ThumbnailFraming framing;
        CHECK(generator->Stage(Guid{}, stage, resources, framing) == editor::ThumbnailStageStep::Failed);
        generator->Unstage(stage); // harmless with nothing staged
    }
    std::filesystem::remove_all(dir, ec);
}
