// Animation editor: cook a SkeletonAsset through its builder into the content DB, then load it back
// through the resource factory and verify the runtime skeleton. Exercises the authoring -> cook ->
// product path (the model importer that fills the source is deferred).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.editor;
import raptor.animation;
import raptor.animation.resource;
import raptor.animation.editor;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::animation;

TEST_CASE("skeleton asset: builder cooks into the content DB, factory loads it back")
{
    GlobalTypeRegistry().Register(SkeletonSource::StaticType());
    RegisterSerializable<SkeletonSource>();
    GlobalTypeRegistry().Register(Skeleton::StaticType());

    FileDelete(u8"raptor_anim_ed_db/skel.rasset");
    RemoveDirectory(u8"raptor_anim_ed_db");
    NativeFileSystem mount(u8"raptor_anim_ed_db");

    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"skel", SkeletonSource::StaticType());
        id = inst->Id();

        // Author a skeleton asset (source populated from a runtime skeleton — model importer later).
        Skeleton skel{ 2 };
        skel.Bones()[0].index = 0; skel.Bones()[0].parentIndex = -1; skel.Bones()[0].name = String{ u8"root" };
        skel.Bones()[1].index = 1; skel.Bones()[1].parentIndex = 0;  skel.Bones()[1].name = String{ u8"child" };
        skel.BuildNameMap(); skel.FindRootBones(); skel.BuildChildIndices(); skel.ComputeInverseBindPoses();

        SkeletonAsset asset;
        asset.fileName = String{ u8"models/char.gltf" };
        SkeletonSource::FromSkeleton(skel, asset.source);

        SkeletonAssetBuilder builder;
        raptor::editor::AssetBuildContext ctx;
        ctx.assetRoot = StringView{};
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    raptor::content::ContentDatabase db(mount);
    SkeletonFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Skeleton> skel = manager.Bind<Skeleton>(id);
    REQUIRE(skel);
    CHECK(skel->BoneCount() == 2);
    CHECK(skel->FindBone(u8"child") == 1);
    CHECK(skel->RootBones().Size() == 1);

    FileDelete(u8"raptor_anim_ed_db/skel.rasset");
    RemoveDirectory(u8"raptor_anim_ed_db");
}
