// Editor - the editor executable (docs/design/editor.md §3.1: the ASSEMBLY point).
// Creates the OS shell + graphics device and runs EditorApplication. Per-subsystem editor
// modules get linked HERE and their RegisterEditor(EditorContext&)
// called on the app's context - the editor core/app libraries never link engine subsystems.
//
// Usage: Tools.Editor [projectDirectory] [--project <dir>]
//   With a project (positional or --project): opens it directly (scaffolding Project.xml +
//   Content/Sources/Cooked/Editor/.cache on first run) - the single-project lifecycle.
//   With NO project: starts on the built-in PROJECT MANAGER (recent projects from the
//   per-user registry, open/create/remove); File > Close Project returns to it.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "Core/Log/Log.h"
#include "Core/Debug/Assert.h"

import foundation.core;
import foundation.shell;
import foundation.shell.desktop;
import foundation.graphics;
import foundation.graphics.gpu;
import foundation.runtime;
import foundation.runtime.client;
import foundation.runtime.desktop;
import engine.scene;
import engine.render;
import foundation.content;
import foundation.animation.resource;
import engine.animation;
import foundation.particles.resource;
import foundation.animation;
import foundation.particles;
import engine.particles;
import foundation.ui.runtime;
import foundation.resource;
import foundation.geometry;
import foundation.geometry.resource;
import foundation.materials.resource;
import foundation.texture.resource;
import pipeline.core;
import pipeline.registration;
import editor.core;
import editor.app;
import editor.scene;
import texture.pipeline;
import fonts.pipeline;
import image.pipeline;
import foundation.image.resource;
import geometry.pipeline;
import animation.pipeline;
import materials.pipeline;
import shaders.pipeline;
import particles.pipeline;
import foundation.input;
import foundation.input.resource;
import input.pipeline;
import editor.input;
import editor.propertyanimation;
import engine.input;
import foundation.physics;
import foundation.physics.resource;
import physics.pipeline;
import foundation.ui.resource;
import ui.pipeline;
import editor.gameui;
import editor.audio;
import editor.texture;
import editor.image;
import editor.fonts;
import editor.physics;
import editor.generic;
import editor.script;
import engine.physics;
import modelimporter;
import foundation.audio;
import foundation.audio.resource;
import audio.pipeline;
import foundation.script;
#ifdef OPTION_HAS_WREN
import foundation.script.wren;
import script.wren.pipeline;
import editor.script.wren;
#endif
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
import script.angelscript.pipeline;
import editor.script.angelscript;
#endif
#ifdef OPTION_HAS_LUAU
import script.luau.pipeline;
import editor.script.luau;
#endif
import foundation.script.resource;
import script.pipeline;

using namespace foundation::core;
namespace shell = foundation::shell;

namespace
{
    // The builder + type registration set lives in Pipeline::Registration (the composition root):
    // RegisterPipelineTypes() + RegisterAllBuilders() below assemble the same set the CLI cooker
    // and export packager use. The editor ADDS its per-language editor-UI services (CodeEditView
    // lexers) on top - those are editor-only and stay host-side.
    void RegisterEditorBuilders(pipeline::BuilderRegistry& registry)
    {
        // Core reflection FIRST: property bindings (property-animation capture/preview) walk
        // TypeOf<Transform>()'s PATCHED properties. Without this the patch only happened
        // lazily when a script manager spun up - a scriptless session never resolved
        // 'Transform.position'. Idempotent.
        foundation::core::RegisterCoreTypes();
        pipeline::RegisterPipelineTypes();
        pipeline::RegisterAllBuilders(registry);
        // Per-language EDITOR-UI services (CodeEditView lexers; completion providers later).
#ifdef OPTION_HAS_WREN
        editor::RegisterWrenEditorUI();
#endif
#ifdef OPTION_HAS_ANGELSCRIPT
        editor::RegisterAngelScriptEditorUI();
#endif
#ifdef OPTION_HAS_LUAU
        editor::RegisterLuauEditorUI();
#endif
    }

    // Create a StaticMeshAsset in the project's Meshes/ group from a procedural primitive,
    // named uniquely (Cube, Cube2, ...). The creator system cooks it right after, so it shows
    // up in the mesh pickers without further steps (quick prototyping, not whiteboxing).
    foundation::content::Instance*
    CreatePrimitiveMeshInstance(editor::EditorContext& ctx, StringView baseName,
                                RefPtr<foundation::geometry::StaticMesh> mesh,
                                foundation::content::Group* target)
    {
        editor::EditorProject* project = ctx.Project();
        if (project == nullptr || mesh.Get() == nullptr)
        {
            return nullptr;
        }
        foundation::content::Group* meshes = target;
        if (meshes == nullptr)
        {
            foundation::content::Group* root = project->SourceDb().RootGroup();
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

        const String name = meshes->UniqueInstanceName(baseName);

        foundation::content::Instance* instance = meshes->CreateInstance(
            name.AsView(), pipeline::StaticMeshAsset::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        pipeline::StaticMeshAsset asset;
        pipeline::MeshImporter::Import(*mesh, asset);
        if (!instance->WriteObject(asset).IsOk())
        {
            return nullptr;
        }
        return instance;
    }

    void RegisterPrimitiveMeshCreators(editor::EditorContext& context)
    {
        namespace geometry = foundation::geometry;
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
                [make, base](editor::EditorContext& ctx, foundation::content::Group* group)
            { return CreatePrimitiveMeshInstance(ctx, base.AsView(), make(), group); };
            context.RegisterCreator(foundation::core::Move(creator));
        }
    }

    // Starter content for a manager-created project: the baseline FONT (and its manifest
    // default - a fresh project must render game-UI text in an export from day one), the
    // default SKY, and the primitive meshes. Payload files come from the baseline-assets dir
    // (the source tree in dev; a relocated editor looks beside the exe).
    [[nodiscard]] String BaselineAssetPath(StringView relative)
    {
        String path(StringView(reinterpret_cast<const utf8char*>(BUILTIN_EDITOR_BASELINE_ASSETS)));
        path = PathJoin(path.AsView(), relative);
        if (FileExists(path.AsView()))
        {
            return path;
        }
        return PathJoin(u8"BaselineAssets", relative); // relocated: staged beside the editor
    }

    void SeedNewProject(editor::EditorContext& ctx, editor::EditorProject& project)
    {
        foundation::content::Group* root = project.SourceDb().RootGroup();

        // 1) The baseline UI font: Roboto imported as a real FontAsset + set as the
        //    manifest's default (the guid the player binds; source guid == product guid).
        {
            const String source = BaselineAssetPath(u8"fonts/roboto/Roboto-Regular.ttf");
            Result<String> copied = pipeline::CopyIntoSources(pipeline::ImportContext{project.SourcesRoot()}, source.AsView());
            if (copied.HasValue())
            {
                foundation::content::Group* fonts = root->GetGroup(u8"Fonts");
                if (fonts == nullptr)
                {
                    fonts = root->CreateGroup(u8"Fonts");
                }
                if (foundation::content::Instance* instance = fonts->CreateInstance(
                        u8"Roboto", pipeline::FontAsset::StaticType()))
                {
                    pipeline::FontAsset asset;
                    asset.fileName = foundation::vfs::SourcePath(copied.Value().AsView());
                    asset.family = String(u8"Roboto");
                    if (instance->WriteObject(asset).IsOk())
                    {
                        project.Settings().defaultUiFontId = instance->Id();
                    }
                }
            }
            else
            {
                LOG_WARNING(u8"Editor",
                                     u8"starter font missing ({}) - new project has no "
                                     u8"default UI font",
                                     source);
            }
        }

        // 2) The default sky: BlueSky.hdr as an equirectangular skybox texture.
        {
            const String source = BaselineAssetPath(u8"environment/BlueSky.hdr");
            Result<String> copied = pipeline::CopyIntoSources(pipeline::ImportContext{project.SourcesRoot()}, source.AsView());
            if (copied.HasValue())
            {
                foundation::content::Group* env = root->GetGroup(u8"Environment");
                if (env == nullptr)
                {
                    env = root->CreateGroup(u8"Environment");
                }
                if (foundation::content::Instance* instance = env->CreateInstance(
                        u8"BlueSky", pipeline::TextureAsset::StaticType()))
                {
                    pipeline::TextureAsset asset;
                    asset.fileName = foundation::vfs::SourcePath(copied.Value().AsView());
                    asset.SetupForEquirectangularSkybox();
                    (void)instance->WriteObject(asset);
                }
            }
        }

        // 3) Primitive meshes (the same creator path as File > New > Primitives).
        (void)CreatePrimitiveMeshInstance(ctx, u8"Cube", foundation::geometry::Primitives::Cube(),
                                          nullptr);
        (void)CreatePrimitiveMeshInstance(ctx, u8"Sphere",
                                          foundation::geometry::Primitives::Sphere(), nullptr);
        (void)CreatePrimitiveMeshInstance(ctx, u8"Plane", foundation::geometry::Primitives::Plane(),
                                          nullptr);

        LOG_INFO(u8"Editor", u8"starter content seeded (font/sky/primitives)");
    }
}
namespace graphics = foundation::graphics;
namespace runtime = foundation::runtime;

// The embedded fallback font (EmbeddedEditorFont.cpp, generated by EmbedBinary.cmake).
extern const unsigned char g_embeddedEditorFont[];
extern const unsigned long long g_embeddedEditorFontSize;

extern "C" const char* BuildStamp();

int main(int argc, char** argv)
{
    // Log capture FIRST (design doc §3.10): the editor buffer + console output go on the global
    // logger before shell/device creation, so early startup logs reach the Console panel.
    editor::EditorLogBuffer logBuffer;
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&logBuffer);
    GlobalLogger().AddSink(&consoleSink);
    LOG_INFO(u8"Build", u8"Editor build {}",
                      reinterpret_cast<const char8_t*>(BuildStamp()));
    GlobalLogger().SetMinLevel(LogLevel::Debug); // the Console panel has a Debug filter toggle

    editor::app::EditorAppConfig config;
    // Project selection: an explicit project (positional arg or --project <dir>) opens
    // directly, Godot-style single-project lifecycle. NO project => the built-in PROJECT
    // MANAGER screen (recent projects, open/create), and File > Close Project returns there.
    if (argc > 1 && argv[1][0] != '-')
    {
        config.projectDirectory = String(StringView(reinterpret_cast<const utf8char*>(argv[1])));
    }
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--project") == 0)
        {
            config.projectDirectory =
                String(StringView(reinterpret_cast<const utf8char*>(argv[i + 1])));
        }
        if (std::strcmp(argv[i], "--exit-after") == 0)
        {
            config.autoExitSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
        if (std::strcmp(argv[i], "--rebuild-after") == 0)
        {
            config.autoRebuildSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
    }
    config.startInProjectManager = config.projectDirectory.IsEmpty();
    config.fontPath =
        String(StringView(reinterpret_cast<const utf8char*>(BUILTIN_EDITOR_FONT_PATH)));
    config.monoFontPath =
        String(StringView(reinterpret_cast<const utf8char*>(BUILTIN_EDITOR_MONO_FONT_PATH)));
    config.embeddedFont = reinterpret_cast<const u8*>(g_embeddedEditorFont);
    config.embeddedFontSize = static_cast<usize>(g_embeddedEditorFontSize);
    config.logBuffer = &logBuffer;

    // Assembly (design doc §3.1): THIS is where the engine subsystems and per-subsystem editor
    // plugins are chosen - the editor core/app libraries never link engine modules; the app
    // drives scene rendering only through the ISceneRenderer interface injected below.
    // Gameplay subsystems are registered by the embedded DefaultApplication against the
    // editor's runtime context (runtime-host.md v3) - the editor registers NONE itself.
    config.seedNewProject = [](editor::EditorContext& ctx, editor::EditorProject& project)
    { SeedNewProject(ctx, project); };
    config.registerEditors = [](editor::app::EditorApplication& app,
                                foundation::runtime::IApplicationHost& host,
                                foundation::ui::runtime::UIHost& uiHost)
    {
        app.SetSceneRenderer(host.Ctx().GetSubsystem<engine::render::RenderSubsystem>());
        editor::RegisterSceneEditor(app.Context(), host, uiHost,
                                              app.EmbeddedApplication());
        editor::RegisterMaterialEditor(app.Context(), host, uiHost);
        editor::RegisterMeshEditor(app.Context(), host, uiHost);
        editor::RegisterParticleEditor(app.Context(), host, uiHost);
        editor::RegisterAnimationGraphEditor(app.Context(), host, uiHost);
        editor::RegisterAnimationClipEditor(app.Context(), host, uiHost);
        editor::RegisterSkeletonEditor(app.Context(), host, uiHost);
        editor::RegisterInputEditor(app.Context(), host);
        editor::RegisterPropertyAnimationEditor(app.Context(), host);
        editor::RegisterGameUIEditor(app.Context(), host, uiHost);
        editor::RegisterAudioClipEditor(app.Context(), host);
        editor::RegisterBusLayoutEditor(app.Context(), host);
        editor::RegisterTextureEditor(app.Context());
        editor::RegisterImageEditor(app.Context());
        editor::RegisterFontEditor(app.Context());
        editor::RegisterCollisionShapeEditor(app.Context());
        // The FALLBACK page registers like any factory: nearest-base dispatch routes every
        // bespoke page first; anything else lands on the generic serialize-driven form
        // instead of the hard "No editor registered" failure.
        editor::RegisterGenericAssetEditor(app.Context());
        // Thumbnail-generator tripwire (asset-thumbnails.md): each domain's Register<X>Editor
        // above also registers its thumbnail generator - bump the expected count when a domain
        // gains one, so a silently-unregistered generator fails loudly here, not as icons.
        DIAGNOSTIC_ASSERT(app.Context().Thumbnails() != nullptr &&
                          app.Context().Thumbnails()->GeneratorCount() == 1);
        // Script behavior page + per-backend "New Asset > <Lang> Script" creators (scripting.md
        // §5). RegisterScriptEditor fans creators over backends that have a registered COOK, so the
        // cooks must be registered FIRST — RegisterAllBuilders (below) also registers them for the
        // cook service, but that runs later, so register them here too (idempotent by languageId).
#ifdef OPTION_HAS_WREN
        pipeline::RegisterWrenScriptCook();
#endif
#ifdef OPTION_HAS_ANGELSCRIPT
        pipeline::RegisterAngelScriptScriptCook();
#endif
#ifdef OPTION_HAS_LUAU
        pipeline::RegisterLuauScriptCook();
#endif
        editor::RegisterScriptEditor(app.Context());
        RegisterPrimitiveMeshCreators(app.Context());
        {
            // New Asset > Input Map: seeded with the conventional Gameplay starter set.
            // (The dedicated editing page is input P2; the asset cooks + binds today.)
            editor::EditorContext::AssetCreator inputCreator;
            inputCreator.label = String(u8"Input Map");
            inputCreator.create =
                [](editor::EditorContext& ctx,
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"InputMap").AsView(), pipeline::InputMapAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::InputMapAsset asset;
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
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"PhysicalMaterial").AsView(), pipeline::PhysicalMaterialAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::PhysicalMaterialAsset asset;
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
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"BusLayout").AsView(), pipeline::AudioBusLayoutAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::AudioBusLayoutAsset asset;
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
                                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"SoundCue").AsView(), pipeline::SoundCueAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::SoundCueAsset asset;
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
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"UIDocument").AsView(), pipeline::UIDocumentAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::UIDocumentAsset asset;
                asset.markup = String(pipeline::kUIDocumentStarter);
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
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance =
                    target->CreateInstance(target->UniqueInstanceName(u8"UITheme").AsView(), pipeline::UIThemeAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::UIThemeAsset asset;
                asset.stylesheet = String(pipeline::kUIThemeStarter);
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
                   foundation::content::Group* group) -> foundation::content::Instance*
            {
                if (ctx.Project() == nullptr)
                {
                    return nullptr;
                }
                foundation::content::Group* target =
                    group != nullptr ? group : ctx.Project()->SourceDb().RootGroup();
                foundation::content::Instance* instance = target->CreateInstance(
                    target->UniqueInstanceName(u8"CollisionShape").AsView(), pipeline::CollisionShapeAsset::StaticType());
                if (instance == nullptr)
                {
                    return nullptr;
                }
                pipeline::CollisionShapeAsset asset;
                if (!instance->WriteObject(asset).IsOk())
                {
                    return nullptr;
                }
                return instance;
            };
            app.Context().RegisterCreator(
                static_cast<editor::EditorContext::AssetCreator&&>(shapeCreator));
        }
        RegisterEditorBuilders(app.Builders()); // the cook service routes through this set

        // OS-file importers (drag-drop onto the editor) - the same set the MCP asset_import tool
        // uses, from the composition root.
        pipeline::RegisterAllImporters(app.Context().Importers());

        // Resource factories come from the embedded DefaultApplication (registered into
        // the editor's preset ResourceManager at its OnStartup) - none registered here.
    };

    LOG_INFO(u8"Editor", u8"starting (project: {})", config.projectDirectory);

    shell::WindowSettings ws;
    ws.title = u8"Editor";
    ws.width = 1600;
    ws.height = 900;

    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "Tools.Editor: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend = graphics::SelectBackendFromArguments(argc, argv);
    gdd.enableValidation = true;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "Tools.Editor: failed to create the graphics device\n");
        return 1;
    }

    editor::app::EditorApplication app(static_cast<editor::app::EditorAppConfig&&>(config));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());

    // The sinks are stack-owned and about to die; detach before returning.
    GlobalLogger().RemoveSink(&logBuffer);
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
