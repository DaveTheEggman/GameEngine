// Draconic Editor - the editor executable (docs/design/editor.md §3.1: the ASSEMBLY point).
// Creates the OS shell + graphics device and runs EditorApplication. Per-subsystem editor
// modules (draconic.<sys>.editor) get linked HERE and their RegisterEditor(EditorContext&)
// called on the app's context - the editor core/app libraries never link engine subsystems.
//
// Usage: RaptorEditor [projectDirectory]
//   Opens the project (scaffolding Project.xml + Content/Sources/Cooked/Editor/.cache on first
//   run). Defaults to ./EditorProject.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "Core/Log/Log.h"

import draconic.core;
import draconic.shell;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.desktop;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.content;
import draconic.animation.resource;
import draconic.animation.subsystem;
import draconic.particles.resource;
import draconic.particles.subsystem;
import draconic.ui.runtime;
import draconic.resource;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.materials.resource;
import draconic.texture.resource;
import draconic.editor;
import draconic.editor.core;
import draconic.editor.app;
import draconic.editor.scene;
import draconic.texture.editor;
import draconic.image.editor;
import draconic.image.resource;
import draconic.geometry.editor;
import draconic.animation.editor;
import draconic.materials.editor;
import draconic.shaders.editor;
import draconic.particles.editor;
import draconic.input;
import draconic.input.resource;
import draconic.input.editor;
import draconic.editor.input;
import draconic.input.subsystem;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.editor;
import draconic.ui.resource;
import draconic.ui.editor;
import draconic.editor.gameui;
import draconic.editor.audio;
import draconic.editor.texture;
import draconic.editor.image;
import draconic.editor.script;
import draconic.physics.subsystem;
import draconic.modelimporter;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.editor;
import draconic.script;
import draconic.script.wren;
import draconic.script.angelscript;
import draconic.script.wren.editor;
import draconic.script.angelscript.editor;
import draconic.script.resource;
import draconic.script.editor;

using namespace draconic::core;
namespace editor = draconic::editor;
namespace shell = draconic::shell;

namespace
{
    template <typename T>
    void AddBuilder(editor::BuilderRegistry& registry)
    {
        registry.Register(
            UniquePtr<editor::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Every engine builder (kept in lockstep with the RaptorCook CLI's set).
    void RegisterAllBuilders(editor::BuilderRegistry& registry)
    {
        draconic::texture::RegisterTextureAsset();
        draconic::image::RegisterImageAsset();
        draconic::geometry::RegisterMeshAssets();
        draconic::animation::RegisterAnimationAssets();
        draconic::materials::RegisterMaterialAsset();
        draconic::shaders::RegisterShaderAsset();
        draconic::particles::RegisterParticleEffectAsset();
        draconic::input::RegisterInputMapAsset();
        draconic::modelimporter::RegisterModelManifestAsset();
        // Product/resource types: ReadObject constructs cooked products BY TYPE NAME, so the
        // runtime-facing types must be registered too (meshes/materials/textures/animation/
        // manifest via the model-importer helper, plus the image resource).
        draconic::model::RegisterModelResourceTypes();
        draconic::image::RegisterImageResource();
        draconic::physics::RegisterPhysicsAssets();
        draconic::physics::RegisterPhysicsResource();
        draconic::ui::RegisterUIAssets();
        draconic::ui::RegisterUIResource();
        draconic::audio::RegisterAudioAssets();
        draconic::audio::RegisterAudioResource();
        draconic::script::RegisterScriptAssets();
        draconic::script::RegisterScriptResource();
        // The builder resolves a per-language COOK through the registry (B3);
        // registering backends + cooks is the entry point's job - both languages.
        draconic::script::wren::RegisterWrenScriptBackend();
        draconic::script::angelscript::RegisterAngelScriptBackend();
        draconic::script::RegisterWrenScriptCook();
        draconic::script::RegisterAngelScriptScriptCook();

        AddBuilder<draconic::texture::TextureAssetBuilder>(registry);
        AddBuilder<draconic::image::ImageAssetBuilder>(registry);
        AddBuilder<draconic::geometry::StaticMeshAssetBuilder>(registry);
        AddBuilder<draconic::geometry::SkinnedMeshAssetBuilder>(registry);
        AddBuilder<draconic::animation::SkeletonAssetBuilder>(registry);
        AddBuilder<draconic::animation::AnimationClipAssetBuilder>(registry);
        AddBuilder<draconic::animation::AnimationGraphAssetBuilder>(registry);
        AddBuilder<draconic::materials::MaterialAssetBuilder>(registry);
        AddBuilder<draconic::shaders::ShaderAssetBuilder>(registry);
        AddBuilder<draconic::particles::ParticleEffectAssetBuilder>(registry);
        AddBuilder<draconic::input::InputMapAssetBuilder>(registry);
        AddBuilder<draconic::modelimporter::ModelManifestAssetBuilder>(registry);
        AddBuilder<draconic::physics::CollisionShapeAssetBuilder>(registry);
        AddBuilder<draconic::physics::PhysicalMaterialAssetBuilder>(registry);
        AddBuilder<draconic::ui::UIDocumentAssetBuilder>(registry);
        AddBuilder<draconic::ui::UIThemeAssetBuilder>(registry);
        AddBuilder<draconic::audio::AudioClipAssetBuilder>(registry);
        AddBuilder<draconic::audio::AudioBusLayoutAssetBuilder>(registry);
        AddBuilder<draconic::audio::SoundCueAssetBuilder>(registry);
        AddBuilder<draconic::script::ScriptClassAssetBuilder>(registry);
    }

    // Create a StaticMeshAsset in the project's Meshes/ group from a procedural primitive,
    // named uniquely (Cube, Cube2, ...). The creator system cooks it right after, so it shows
    // up in the mesh pickers without further steps (quick prototyping, not whiteboxing).
    draconic::content::Instance*
    CreatePrimitiveMeshInstance(editor::EditorContext& ctx, StringView baseName,
                                RefPtr<draconic::geometry::StaticMesh> mesh,
                                draconic::content::Group* target)
    {
        editor::EditorProject* project = ctx.Project();
        if (project == nullptr || mesh.Get() == nullptr)
        {
            return nullptr;
        }
        draconic::content::Group* meshes = target;
        if (meshes == nullptr)
        {
            draconic::content::Group* root = project->SourceDb().RootGroup();
            meshes = root->GetGroup(u8"Meshes");
            if (meshes == nullptr)
            {
                meshes = root->CreateGroup(u8"Meshes");
            }
        }
        if (meshes == nullptr)
        {
            return nullptr;
        }

        String name(baseName);
        for (i32 counter = 2; meshes->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(baseName);
            if (counter >= 10)
            {
                name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10)));
            }
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
        }

        draconic::content::Instance* instance = meshes->CreateInstance(
            name.AsView(), draconic::geometry::StaticMeshAsset::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        draconic::geometry::StaticMeshAsset asset;
        draconic::geometry::MeshImporter::Import(*mesh, asset);
        if (!instance->WriteObject(asset).IsOk())
        {
            return nullptr;
        }
        return instance;
    }

    void RegisterPrimitiveMeshCreators(editor::EditorContext& context)
    {
        namespace geometry = draconic::geometry;
        struct Entry
        {
            const utf8char* label;
            RefPtr<geometry::StaticMesh> (*make)();
        };
        static const Entry entries[] = {
            {u8"Cube", []() { return geometry::Primitives::Cube(); }},
            {u8"Sphere", []() { return geometry::Primitives::Sphere(); }},
            {u8"Plane", []() { return geometry::Primitives::Plane(); }},
            {u8"Cylinder", []() { return geometry::Primitives::Cylinder(); }},
            {u8"Cone", []() { return geometry::Primitives::Cone(); }},
            {u8"Torus", []() { return geometry::Primitives::Torus(); }},
        };
        for (const Entry& e : entries)
        {
            editor::EditorContext::AssetCreator creator;
            creator.label = String(StringView(e.label));
            creator.category = String(StringView(u8"Primitives"));
            auto make = e.make;
            String base(StringView(e.label));
            creator.create =
                [make, base](editor::EditorContext& ctx, draconic::content::Group* group)
            { return CreatePrimitiveMeshInstance(ctx, base.AsView(), make(), group); };
            context.RegisterCreator(draconic::core::Move(creator));
        }
    }
}
namespace graphics = draconic::graphics;
namespace runtime = draconic::runtime;

int main(int argc, char** argv)
{
    // Log capture FIRST (design doc §3.10): the editor buffer + console output go on the global
    // logger before shell/device creation, so early startup logs reach the Console panel.
    draconic::editor::EditorLogBuffer logBuffer;
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&logBuffer);
    GlobalLogger().AddSink(&consoleSink);
    GlobalLogger().SetMinLevel(LogLevel::Debug); // the Console panel has a Debug filter toggle

    editor::app::EditorAppConfig config;
    config.projectDirectory = String(argc > 1 && argv[1][0] != '-'
                                         ? StringView(reinterpret_cast<const utf8char*>(argv[1]))
                                         : StringView(u8"EditorProject"));
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--exit-after") == 0)
        {
            config.autoExitSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
        if (std::strcmp(argv[i], "--rebuild-after") == 0)
        {
            config.autoRebuildSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
    }
    config.fontPath =
        String(StringView(reinterpret_cast<const utf8char*>(DRACONIC_EDITOR_FONT_PATH)));
    config.logBuffer = &logBuffer;

    // Assembly (design doc §3.1): THIS is where the engine subsystems and per-subsystem editor
    // plugins are chosen - the editor core/app libraries never link engine modules; the app
    // drives scene rendering only through the ISceneRenderer interface injected below.
    // Gameplay subsystems are registered by the embedded DefaultApplication against the
    // editor's runtime context (runtime-host.md v3) - the editor registers NONE itself.
    config.registerEditors = [](editor::app::EditorApplication& app,
                                draconic::runtime::IApplicationHost& host,
                                draconic::ui::runtime::UIHost& uiHost)
    {
        app.SetSceneRenderer(host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>());
        draconic::editor::RegisterSceneEditor(app.Context(), host, uiHost,
                                              app.EmbeddedApplication());
        draconic::editor::RegisterMaterialEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterMeshEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterParticleEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterAnimationGraphEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterAnimationClipEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterSkeletonEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterInputEditor(app.Context(), host);
        draconic::editor::RegisterGameUIEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterAudioClipEditor(app.Context(), host);
        draconic::editor::RegisterBusLayoutEditor(app.Context(), host);
        draconic::editor::RegisterTextureEditor(app.Context());
        draconic::editor::RegisterImageEditor(app.Context());
        // Script behavior page + per-backend "New Asset > <Lang> Script" creators (scripting.md
        // §5). RegisterScriptEditor fans creators over backends that have a registered COOK, so the
        // cooks must be registered FIRST — RegisterAllBuilders (below) also registers them for the
        // cook service, but that runs later, so register them here too (idempotent by languageId).
        draconic::script::RegisterWrenScriptCook();
        draconic::script::RegisterAngelScriptScriptCook();
        draconic::editor::RegisterScriptEditor(app.Context());
        RegisterPrimitiveMeshCreators(app.Context());
        {
            // New Asset > Input Map: seeded with the conventional Gameplay starter set.
            // (The dedicated editing page is input P2; the asset cooks + binds today.)
            editor::EditorContext::AssetCreator inputCreator;
            inputCreator.label = String(u8"Input Map");
            inputCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"InputMap", draconic::input::InputMapAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::input::InputMapAsset asset;
                asset.SeedDefaultContent();
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(inputCreator));
        }
        {
            // New Asset > Physical Material (surface properties; edited in the inspector).
            editor::EditorContext::AssetCreator materialCreator;
            materialCreator.label = String(u8"Physical Material");
            materialCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"PhysicalMaterial", draconic::physics::PhysicalMaterialAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::physics::PhysicalMaterialAsset asset;
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(materialCreator));

            // New Asset > Audio Bus Layout (the mixer as data; edited in the inspector).
            editor::EditorContext::AssetCreator busLayoutCreator;
            busLayoutCreator.label = String(u8"Audio Bus Layout");
            busLayoutCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"BusLayout", draconic::audio::AudioBusLayoutAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::audio::AudioBusLayoutAsset asset;
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(busLayoutCreator));

            // New Asset > Sound Cue (weighted clip variants; edited via SoundCuePage).
            editor::EditorContext::AssetCreator cueCreator;
            cueCreator.label = String(u8"Sound Cue");
            cueCreator.create = [](editor::EditorContext& ctx,
                                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"SoundCue", draconic::audio::SoundCueAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::audio::SoundCueAsset asset;
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(cueCreator));
            // New Asset > <Language> Script is registered by RegisterScriptEditor (one creator
            // per script backend, seeded from the cook's NewAssetTemplate - backend-neutral).
        }
        {
            // New Asset > UI Document / UI Theme (starter payloads; edited as text until
            // the UIDocumentPage lands, hot-reloading through the standard cook).
            editor::EditorContext::AssetCreator documentCreator;
            documentCreator.label = String(u8"UI Document");
            documentCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"UIDocument", draconic::ui::UIDocumentAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::ui::UIDocumentAsset asset;
                asset.markup = String(draconic::ui::kUIDocumentStarter);
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(documentCreator));
            editor::EditorContext::AssetCreator themeCreator;
            themeCreator.label = String(u8"UI Theme");
            themeCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance =
                    target->CreateInstance(u8"UITheme", draconic::ui::UIThemeAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::ui::UIThemeAsset asset;
                asset.stylesheet = String(draconic::ui::kUIThemeStarter);
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(themeCreator));
        }
        {
            // New Asset > Collision Shape (point its sourceMesh at a mesh in the inspector).
            editor::EditorContext::AssetCreator shapeCreator;
            shapeCreator.label = String(u8"Collision Shape");
            shapeCreator.create =
                [](editor::EditorContext& ctx,
                   draconic::content::Group* group) -> draconic::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                draconic::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"CollisionShape", draconic::physics::CollisionShapeAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                draconic::physics::CollisionShapeAsset asset;
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(shapeCreator));
        }
        RegisterAllBuilders(app.Builders()); // the cook service routes through this set

        // OS-file importers (drag-drop onto the editor).
        app.Context().Importers().Register(UniquePtr<editor::IFileImporter>(
            DefaultAllocator().New<draconic::texture::TextureFileImporter>(), DefaultAllocator()));
        app.Context().Importers().Register(UniquePtr<editor::IFileImporter>(
            DefaultAllocator().New<draconic::modelimporter::ModelFileImporter>(),
            DefaultAllocator()));
        app.Context().Importers().Register(UniquePtr<editor::IFileImporter>(
            DefaultAllocator().New<draconic::ui::UIFileImporter>(), DefaultAllocator()));
        app.Context().Importers().Register(UniquePtr<editor::IFileImporter>(
            DefaultAllocator().New<draconic::audio::AudioFileImporter>(), DefaultAllocator()));
        app.Context().Importers().Register(UniquePtr<editor::IFileImporter>(
            DefaultAllocator().New<draconic::script::ScriptFileImporter>(), DefaultAllocator()));

        // Resource factories come from the embedded DefaultApplication (registered into
        // the editor's preset ResourceManager at its OnStartup) - none registered here.
    };

    DRACONIC_LOG_INFO(u8"Editor", u8"starting (project: {})", config.projectDirectory);

    shell::WindowSettings ws;
    ws.title = u8"Draconic Editor";
    ws.width = 1600;
    ws.height = 900;

    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend = graphics::BackendType::Vulkan;
    gdd.enableValidation = true;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the graphics device\n");
        return 1;
    }

    editor::app::EditorApplication app(static_cast<editor::app::EditorAppConfig&&>(config));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());

    // The sinks are stack-owned and about to die; detach before returning.
    GlobalLogger().RemoveSink(&logBuffer);
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
