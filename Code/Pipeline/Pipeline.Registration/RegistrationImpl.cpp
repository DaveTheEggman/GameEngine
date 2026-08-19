// Pipeline::Registration - implementation unit.
//
// The wide fan-in lives HERE (one TU), keeping the interface BMI lean for the four hosts. This
// is the ONLY place the engine's full builder/importer/type set is written down - hosts call the
// three entry points, never restate the list.

module;
#include "Core/Prelude.h"

module pipeline.registration;

import foundation.core;
import foundation.content;
import foundation.animation;
import foundation.particles;
import foundation.scene;
import foundation.scene.resource;
import pipeline.core;
import pipeline.importer;
import texture.pipeline;
import fonts.pipeline;
import image.pipeline;
import foundation.image.resource;
import geometry.pipeline;
import animation.pipeline;
import propertyanimation.pipeline;
import materials.pipeline;
import shaders.pipeline;
import particles.pipeline;
import foundation.input;
import foundation.input.resource;
import input.pipeline;
import modelimporter;
import foundation.physics;
import foundation.physics.resource;
import physics.pipeline;
import foundation.navigation.resource;
import navigation.pipeline;
import foundation.ui.resource;
import ui.pipeline;
import foundation.audio;
import foundation.audio.resource;
import audio.pipeline;
import foundation.script;
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
import script.angelscript.pipeline;
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
import script.luau.pipeline;
#endif
import foundation.script.resource;
import script.pipeline;

using namespace foundation::core;

namespace pipeline
{
    namespace
    {
        template <typename T>
        void AddBuilder(BuilderRegistry& registry)
        {
            registry.Register(
                UniquePtr<IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
        }

        template <typename T>
        void AddImporter(ImporterRegistry& registry)
        {
            registry.Register(
                UniquePtr<IFileImporter>(DefaultAllocator().New<T>(), DefaultAllocator()));
        }
    }

    void RegisterPipelineTypes()
    {
        RegisterAssetReflection(); // base Asset::fileName + SourcePath
        RegisterTextureAsset();
        RegisterFontAsset(); // asset + FontResource product
        RegisterImageAsset();
        RegisterMeshAssets();
        RegisterAnimationAssets();
        RegisterPropertyAnimationAssets();
        RegisterMaterialAsset();
        RegisterShaderAsset();
        RegisterParticleEffectAsset();
        RegisterInputMapAsset();
        RegisterModelManifestAsset();
        // Product/resource types: ReadObject constructs cooked products BY TYPE NAME, so the
        // runtime-facing types must be registered too (meshes/materials/textures/animation/
        // manifest via the model-importer helper, plus the image resource).
        foundation::model::RegisterModelResourceTypes();
        foundation::image::RegisterImageResource();
        RegisterPhysicsAssets();
        foundation::physics::RegisterPhysicsResource();
        RegisterNavigationZoneAsset();
        foundation::navigation::RegisterNavigationResource();
        RegisterUIAssets();
        foundation::ui::RegisterUIResource();
        RegisterAudioAssets();
        foundation::audio::RegisterAudioResource();
        RegisterScriptAssets();
        foundation::script::RegisterScriptResource();
        // The builder resolves a per-language COOK through the registry (B3); registering
        // backends + cooks is the composition root's job - both languages.
#ifdef OPTION_HAS_ANGELSCRIPT
        foundation::script::angelscript::RegisterAngelScriptBackend();
        RegisterAngelScriptScriptCook();
#endif
#ifdef OPTION_HAS_LUAU
        foundation::script::RegisterLuauScriptBackend();
        RegisterLuauScriptCook();
#endif
        // Scenes are packed/read as SceneDocument (export staging + a headless scene cook path):
        // register the type + its serializer so ReadObject/WriteObject round-trip them.
        GlobalTypeRegistry().Register(foundation::scene::SceneDocument::StaticType());
        RegisterSerializable<foundation::scene::SceneDocument>();
    }

    void RegisterAllBuilders(BuilderRegistry& registry)
    {
        AddBuilder<TextureAssetBuilder>(registry);
        AddBuilder<FontAssetBuilder>(registry);
        AddBuilder<ImageAssetBuilder>(registry);
        AddBuilder<StaticMeshAssetBuilder>(registry);
        AddBuilder<SkinnedMeshAssetBuilder>(registry);
        AddBuilder<SkeletonAssetBuilder>(registry);
        AddBuilder<AnimationClipAssetBuilder>(registry);
        AddBuilder<AnimationGraphAssetBuilder>(registry);
        AddBuilder<PropertyAnimationClipAssetBuilder>(registry);
        AddBuilder<MaterialAssetBuilder>(registry);
        AddBuilder<ShaderAssetBuilder>(registry);
        AddBuilder<ParticleEffectAssetBuilder>(registry);
        AddBuilder<InputMapAssetBuilder>(registry);
        AddBuilder<ModelManifestAssetBuilder>(registry);
        AddBuilder<CollisionShapeAssetBuilder>(registry);
        AddBuilder<PhysicalMaterialAssetBuilder>(registry);
        AddBuilder<NavigationZoneAssetBuilder>(registry);
        AddBuilder<UIDocumentAssetBuilder>(registry);
        AddBuilder<UIThemeAssetBuilder>(registry);
        AddBuilder<AudioClipAssetBuilder>(registry);
        AddBuilder<AudioBusLayoutAssetBuilder>(registry);
        AddBuilder<SoundCueAssetBuilder>(registry);
        AddBuilder<ScriptClassAssetBuilder>(registry);
    }

    void RegisterAllImporters(ImporterRegistry& registry)
    {
        AddImporter<TextureFileImporter>(registry);
        AddImporter<ModelFileImporter>(registry);
        AddImporter<UIFileImporter>(registry);
        AddImporter<AudioFileImporter>(registry);
        AddImporter<ScriptFileImporter>(registry);
        AddImporter<FontAssetImporter>(registry);
        AddImporter<ImageFileImporter>(registry);
    }
}
