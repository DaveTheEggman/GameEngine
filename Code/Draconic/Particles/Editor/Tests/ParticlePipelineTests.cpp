// The full authoring bake: author a ParticleEffectAsset in code, cook it with
// ParticleEffectAssetBuilder into a content-DB instance, then load the cooked ParticleEffectResource
// back through the ResourceManager + factory and confirm it round-trips + simulates. Mirrors
// TexturePipelineTests, adapted for an AUTHORED (not imported) asset.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.editor;
import draconic.particles;
import draconic.particles.resource;
import draconic.particles.editor;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::resource;
using namespace draconic::particles;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"draconic_pfx_edit_db/smoke.rasset");
        RemoveDirectory(u8"draconic_pfx_edit_db");
    }
}

TEST_CASE("particles.pipeline: authored asset -> Build() -> cooked resource -> Bind -> run")
{
    RegisterParticleEffectAsset();
    RemoveTree();

    NativeFileSystem mount(u8"draconic_pfx_edit_db");
    Guid id;

    // Author the effect in an asset, then cook it via the builder into the DB instance.
    {
        draconic::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"smoke", ParticleEffectResource::StaticType());
        id = inst->Id();

        ParticleEffectAsset asset;
        ParticleSystem& sys = asset.Effect().AddSystem(1000, 777ull);
        sys.name = String(u8"smoke");
        sys.blendMode = ParticleBlendMode::Alpha;
        sys.emitter.spawnRate = 40.0f;
        sys.AddInitializer<LifetimeInitializer>().lifetime = RangeFloat(2.0f, 3.0f);
        sys.AddInitializer<SizeInitializer>().size = RangeVector2::Constant(Vector2{ 1.0f, 1.0f });
        sys.AddBehavior<DragBehavior>().drag = 0.3f;
        sys.AddBehavior<SizeOverLifetimeBehavior>().curve =
            ParticleCurveVector2::Linear(Vector2{ 1, 1 }, Vector2{ 3, 3 });

        ParticleEffectAssetBuilder builder;
        draconic::editor::AssetBuildContext ctx{ StringView{}, inst };
        REQUIRE(builder.AssetType() == &ParticleEffectAsset::StaticType());
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // Load the cooked resource back.
    draconic::content::ContentDatabase db(mount);
    ParticleEffectFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<ParticleEffectResource> res = manager.Bind<ParticleEffectResource>(id);
    REQUIRE(res);
    ParticleEffect& fx = res->Effect();
    REQUIRE(fx.SystemCount() == 1);

    ParticleSystem* sys = fx.GetSystem(0);
    REQUIRE(sys != nullptr);
    CHECK(sys->MaxParticles() == 1000);
    CHECK(sys->Seed() == 777ull);
    CHECK(sys->blendMode == ParticleBlendMode::Alpha);
    CHECK(sys->emitter.spawnRate == doctest::Approx(40.0f));
    CHECK(sys->InitializerCount() == 2);
    CHECK(sys->BehaviorCount() == 2);

    ParticleEffectInstance inst(fx);
    inst.Update(0.1f);
    CHECK(sys->AliveCount() > 0);

    RemoveTree();
}
