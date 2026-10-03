// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Scene settings from a profile: while a block's source is a profile, an edit of a value field
// lands in the profile (the block's own fields stay the scene's) and undoes; Copy Into Scene and a
// source switch are one undo step each; Make Profile writes a profile asset from the block's values
// and switches to it; a profile-mode edit is queued for the save flow and written to the asset.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.content;
import foundation.scene;
import foundation.render;
import engine.render;
import pipeline.core;
import render.pipeline;
import editor.core;
import editor.scene;

using namespace foundation::core;
using namespace engine::render;
namespace scene = foundation::scene;
using editor::SceneEditContext;

namespace
{
    const TypeInfo* EnvType() { return &TypeOf<EnvironmentSettings>(); }

    // A scene whose environment uses `profile` (a loaded product under `id`).
    struct ProfiledScene
    {
        scene::Scene scene{DefaultAllocator(), u8"Level"};
        EnvironmentSystem* env = nullptr;
        RefPtr<EnvironmentProfile> profile = MakeRef<EnvironmentProfile>(DefaultAllocator());
        editor::EditorCommandStack commands;
        SceneEditContext edit{scene, commands};
        Guid id{0x51u, 0x7Eu};

        ProfiledScene()
        {
            RegisterRenderComponentReflection();
            env = scene.AddSystem<EnvironmentSystem>();
            env->Environment().ambientIntensity = 0.25f;
            profile->values.ambientIntensity = 0.9f;
            env->Environment().source = SettingsSource::Profile;
            env->Environment().profile = profile.Get();
            env->Environment().profile.SetId(id);
        }
    };
}

TEST_CASE("settings profiles: a value edit lands in the profile, the block's own fields in the scene")
{
    ProfiledScene s;
    Array<Guid> edited;
    s.edit.OnSettingsProfileEdited = [&edited](const TypeInfo*, const Guid& profile)
    { edited.PushBack(profile); };

    s.edit.SetSceneSettingProperty(EnvType(), "ambientIntensity", Variant::From<f32>(0.7f));
    CHECK(s.profile->values.ambientIntensity == doctest::Approx(0.7f));
    CHECK(s.env->Environment().ambientIntensity == doctest::Approx(0.25f));
    REQUIRE(edited.Size() == 1);
    CHECK(edited[0] == s.id);

    // An enum, written raw, goes the same way.
    s.edit.SetSceneSettingPropertyRaw(EnvType(), "skyMode",
                                      static_cast<i64>(foundation::render::SkyMode::Analytic));
    CHECK(s.profile->values.skyMode == foundation::render::SkyMode::Analytic);

    s.commands.Undo();
    s.commands.Undo();
    CHECK(s.profile->values.ambientIntensity == doctest::Approx(0.9f));
    CHECK(s.profile->values.skyMode != foundation::render::SkyMode::Analytic);
    CHECK(edited.Size() == 4); // every write to the profile is persisted, the undos too

    // The source is the scene's ("sceneOnly"): switching it edits the block, and after it an edit
    // lands in the scene's own values.
    s.edit.SetSceneSettingPropertyRaw(EnvType(), "source", static_cast<i64>(SettingsSource::Scene));
    CHECK(s.env->Environment().source == SettingsSource::Scene);
    s.edit.SetSceneSettingProperty(EnvType(), "ambientIntensity", Variant::From<f32>(0.4f));
    CHECK(s.env->Environment().ambientIntensity == doctest::Approx(0.4f));
    CHECK(s.profile->values.ambientIntensity == doctest::Approx(0.9f));
    CHECK(edited.Size() == 4);
    s.commands.Undo();
    s.commands.Undo();
    CHECK(s.env->Environment().source == SettingsSource::Profile);
    CHECK(s.env->Environment().ambientIntensity == doctest::Approx(0.25f));
}

TEST_CASE("settings profiles: Copy Into Scene and a source switch are one undo step each")
{
    ProfiledScene s;
    s.profile->values.turbidity = 6.0f;

    REQUIRE(s.edit.MutateSceneSettings(EnvType(), [](scene::SceneSystem& system)
                                       { system.CopySettingsProfileIntoScene(); }));
    CHECK(s.env->Environment().source == SettingsSource::Scene);
    CHECK(s.env->Environment().ambientIntensity == doctest::Approx(0.9f));
    CHECK(s.env->Environment().turbidity == doctest::Approx(6.0f));
    CHECK(s.env->Environment().profile.id == s.id); // kept: the profile is a pick away
    s.commands.Undo();
    CHECK(s.env->Environment().source == SettingsSource::Profile);
    CHECK(s.env->Environment().ambientIntensity == doctest::Approx(0.25f));

    const Guid other{0x99u, 0x1u};
    REQUIRE(s.edit.MutateSceneSettings(EnvType(), [other](scene::SceneSystem& system)
                                       { system.UseSettingsProfile(other); }));
    CHECK(s.env->Environment().profile.id == other);
    s.commands.Undo();
    CHECK(s.env->Environment().profile.id == s.id);
}

TEST_CASE("settings profiles: Make Profile writes the values and switches; an edit reaches the asset")
{
    pipeline::RegisterRenderProfileAssets();
    RegisterRenderComponentReflection();
    const StringView dir = u8"scratch_editor_settings_profiles";
    (void)RemoveDirectoryRecursive(dir);
    REQUIRE(editor::EditorProject::Create(DefaultAllocator(), dir, u8"P").IsOk());
    UniquePtr<editor::EditorProject> project = editor::EditorProject::Open(DefaultAllocator(), dir);
    REQUIRE(static_cast<bool>(project));
    {
        editor::EditorContext context(DefaultAllocator());
        context.SetProject(project.Get());
        pipeline::RegisterRenderCreators(context.Creators());
        // The join the app wires from its builders: the environment profile's asset.
        context.SourceAssetTypesOf = [](const TypeInfo& product)
        {
            Array<const TypeInfo*> out;
            if (&product == &EnvironmentProfile::StaticType())
            {
                out.PushBack(&pipeline::EnvironmentProfileAsset::StaticType());
            }
            return out;
        };

        scene::Scene scene(DefaultAllocator(), u8"Level");
        auto* env = scene.AddSystem<EnvironmentSystem>();
        env->Environment().ambientIntensity = 0.3f;
        env->Environment().turbidity = 5.0f;
        editor::EditorCommandStack commands;
        SceneEditContext edit(scene, commands);

        foundation::content::Instance* made =
            editor::MakeSettingsProfile(context, edit, EnvType(), u8"Level Environment");
        REQUIRE(made != nullptr);
        CHECK(made->Path() == u8"Profiles/Level Environment");
        const Guid id = made->Id();
        {
            RefPtr<ISerializable> object = made->ReadObject();
            auto* asset = Cast<pipeline::EnvironmentProfileAsset>(object.Get());
            REQUIRE(asset != nullptr);
            CHECK(asset->values.ambientIntensity == doctest::Approx(0.3f));
            CHECK(asset->values.turbidity == doctest::Approx(5.0f));
            CHECK(asset->values.source == SettingsSource::Scene); // a profile has no source
        }
        CHECK(env->Environment().source == SettingsSource::Profile);
        CHECK(env->Environment().profile.id == id);
        commands.Undo();
        CHECK(env->Environment().source == SettingsSource::Scene);
        commands.Redo();

        // The profile loaded (here by hand; the editor binds the cooked product): an edit in
        // profile mode is queued and the save flow writes it to the asset.
        RefPtr<EnvironmentProfile> product = MakeRef<EnvironmentProfile>(DefaultAllocator());
        product->values = env->Environment();
        env->Environment().profile = product.Get();
        env->Environment().profile.SetId(id);
        edit.OnSettingsProfileEdited = [&](const TypeInfo* type, const Guid& profile)
        {
            if (scene::SceneSystem* system = edit.FindSystemBySettingsType(type))
            {
                editor::QueueSettingsProfileEdit(context, *system, profile);
            }
        };
        edit.SetSceneSettingProperty(EnvType(), "turbidity", Variant::From<f32>(8.0f));
        CHECK(product->values.turbidity == doctest::Approx(8.0f));
        REQUIRE(context.HasPendingAssetEdits());
        REQUIRE(context.DrainAssetEdits(project->SourceDb()).IsOk());
        {
            RefPtr<ISerializable> object = project->SourceDb().GetInstance(id)->ReadObject();
            auto* asset = Cast<pipeline::EnvironmentProfileAsset>(object.Get());
            REQUIRE(asset != nullptr);
            CHECK(asset->values.turbidity == doctest::Approx(8.0f));
            CHECK(asset->values.ambientIntensity == doctest::Approx(0.3f));
        }
        context.SetProject(nullptr);
    }
    project.Reset();
    (void)RemoveDirectoryRecursive(dir);
}
