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
import draconic.physics.subsystem;
import draconic.modelimporter;

using namespace draconic::core;
namespace ed = draconic::editor;
namespace shell = draconic::shell;

namespace
{
    template <typename T>
    void AddBuilder(ed::BuilderRegistry& registry)
    {
        registry.Register(UniquePtr<ed::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Every engine builder (kept in lockstep with the RaptorCook CLI's set).
    void RegisterAllBuilders(ed::BuilderRegistry& registry)
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
        draconic::modelimporter::RegisterModelImporterTypes();
        draconic::image::RegisterImageResource();

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
    }

    // Create a StaticMeshAsset in the project's Meshes/ group from a procedural primitive,
    // named uniquely (Cube, Cube2, ...). The creator system cooks it right after, so it shows
    // up in the mesh pickers without further steps (quick prototyping, not whiteboxing).
    draconic::content::Instance* CreatePrimitiveMeshInstance(
        ed::EditorContext& ctx, StringView baseName, RefPtr<draconic::geometry::StaticMesh> mesh,
        draconic::content::Group* target)
    {
        ed::EditorProject* project = ctx.Project();
        if (project == nullptr || mesh.Get() == nullptr) { return nullptr; }
        draconic::content::Group* meshes = target;
        if (meshes == nullptr)
        {
            draconic::content::Group* root = project->SourceDb().RootGroup();
            meshes = root->GetGroup(u8"Meshes");
            if (meshes == nullptr) { meshes = root->CreateGroup(u8"Meshes"); }
        }
        if (meshes == nullptr) { return nullptr; }

        String name(baseName);
        for (i32 counter = 2; meshes->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(baseName);
            if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
        }

        draconic::content::Instance* instance =
            meshes->CreateInstance(name.AsView(), draconic::geometry::StaticMeshAsset::StaticType());
        if (instance == nullptr) { return nullptr; }
        draconic::geometry::StaticMeshAsset asset;
        draconic::geometry::MeshImporter::Import(*mesh, asset);
        if (!instance->WriteObject(asset).IsOk()) { return nullptr; }
        return instance;
    }

    void RegisterPrimitiveMeshCreators(ed::EditorContext& context)
    {
        namespace geo = draconic::geometry;
        struct Entry { const utf8char* label; RefPtr<geo::StaticMesh> (*make)(); };
        static const Entry entries[] = {
            { u8"Cube",     []() { return geo::Primitives::Cube(); } },
            { u8"Sphere",   []() { return geo::Primitives::Sphere(); } },
            { u8"Plane",    []() { return geo::Primitives::Plane(); } },
            { u8"Cylinder", []() { return geo::Primitives::Cylinder(); } },
            { u8"Cone",     []() { return geo::Primitives::Cone(); } },
            { u8"Torus",    []() { return geo::Primitives::Torus(); } },
        };
        for (const Entry& e : entries)
        {
            ed::EditorContext::AssetCreator creator;
            creator.label = String(StringView(e.label));
            creator.category = String(StringView(u8"Primitives"));
            auto make = e.make;
            String base(StringView(e.label));
            creator.create = [make, base](ed::EditorContext& ctx, draconic::content::Group* group) {
                return CreatePrimitiveMeshInstance(ctx, base.AsView(), make(), group);
            };
            context.RegisterCreator(draconic::core::Move(creator));
        }
    }
}
namespace graphics = draconic::graphics;
namespace runtime = draconic::runtime;
namespace edapp = draconic::editor::app;

int main(int argc, char** argv)
{
    // Log capture FIRST (design doc §3.10): the editor buffer + console output go on the global
    // logger before shell/device creation, so early startup logs reach the Console panel.
    draconic::editor::EditorLogBuffer logBuffer;
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&logBuffer);
    GlobalLogger().AddSink(&consoleSink);
    GlobalLogger().SetMinLevel(LogLevel::Debug);   // the Console panel has a Debug filter toggle

    edapp::EditorAppConfig config;
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
    config.fontPath = String(StringView(reinterpret_cast<const utf8char*>(DRACONIC_EDITOR_FONT_PATH)));
    config.logBuffer = &logBuffer;

    // Assembly (design doc §3.1): THIS is where the engine subsystems and per-subsystem editor
    // plugins are chosen - the editor core/app libraries never link engine modules; the app
    // drives scene rendering only through the ISceneRenderer interface injected below.
    config.configureEngine = [](draconic::runtime::IApplicationHost& host) {
        // Order matters: scene first, render registers as ISceneAware in OnReady, animation
        // needs the render managers.
        host.Ctx().AddSubsystem<draconic::scene::SceneSubsystem>();
        host.Ctx().AddSubsystem<draconic::render::RenderSubsystem>(
            *host.Graphics()->Raw(), host.Graphics()->FramesInFlight());
        host.Ctx().AddSubsystem<draconic::animation::AnimationSubsystem>();
        host.Ctx().AddSubsystem<draconic::particles::ParticleSubsystem>();
        host.Ctx().AddSubsystem<draconic::physics::PhysicsSubsystem>();
        host.Ctx().AddSubsystem<draconic::input::InputSubsystem>(
            host.Shell() != nullptr ? host.Shell()->Input() : nullptr);
    };
    config.registerEditors = [](edapp::EditorApplication& app,
                                draconic::runtime::IApplicationHost& host,
                                draconic::ui::runtime::UIHost& uiHost) {
        app.SetSceneRenderer(host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>());
        draconic::editor::RegisterSceneEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterMaterialEditor(app.Context(), host, uiHost);
        draconic::editor::RegisterInputEditor(app.Context(), host);
        RegisterPrimitiveMeshCreators(app.Context());
        {
            // New Asset > Input Map: seeded with the conventional Gameplay starter set.
            // (The dedicated editing page is input P2; the asset cooks + binds today.)
            ed::EditorContext::AssetCreator inputCreator;
            inputCreator.label = String(u8"Input Map");
            inputCreator.create = [](ed::EditorContext& ctx, draconic::content::Group* group)
                -> draconic::content::Instance* {
                if (ctx.Project() == nullptr) { return nullptr; }
                draconic::content::Group* target = group != nullptr
                    ? group : ctx.Project()->SourceDb().RootGroup();
                draconic::content::Instance* instance = target->CreateInstance(
                    u8"InputMap", draconic::input::InputMapAsset::StaticType());
                if (instance == nullptr) { return nullptr; }
                draconic::input::InputMapAsset asset;
                asset.SeedDefaultContent();
                if (!instance->WriteObject(asset).IsOk()) { return nullptr; }
                return instance;
            };
            app.Context().RegisterCreator(static_cast<ed::EditorContext::AssetCreator&&>(inputCreator));
        }
        RegisterAllBuilders(app.Builders());   // the cook service routes through this set

        // OS-file importers (drag-drop onto the editor).
        app.Context().Importers().Register(UniquePtr<ed::IFileImporter>(
            DefaultAllocator().New<draconic::texture::TextureFileImporter>(), DefaultAllocator()));
        app.Context().Importers().Register(UniquePtr<ed::IFileImporter>(
            DefaultAllocator().New<draconic::modelimporter::ModelFileImporter>(), DefaultAllocator()));

        // Runtime resource factories (scene refs + inspector pickers resolve through these).
        namespace res = draconic::resource;
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::geometry::StaticMeshFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::geometry::SkinnedMeshFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::materials::MaterialFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::animation::SkeletonFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::animation::AnimationClipFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::animation::AnimationGraphFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::particles::ParticleEffectFactory>(), DefaultAllocator()));
        app.AddResourceFactory(UniquePtr<res::IResourceFactory>(
            DefaultAllocator().New<draconic::texture::TextureFactory>(*host.Graphics()->Raw()),
            DefaultAllocator()));
    };

    DRACONIC_LOG_INFO(u8"Editor", u8"starting (project: {})", config.projectDirectory);

    shell::WindowSettings ws;
    ws.title  = u8"Draconic Editor";
    ws.width  = 1600;
    ws.height = 900;

    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend          = graphics::BackendType::Vulkan;
    gdd.enableValidation = true;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the graphics device\n");
        return 1;
    }

    edapp::EditorApplication app(static_cast<edapp::EditorAppConfig&&>(config));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());

    // The sinks are stack-owned and about to die; detach before returning.
    GlobalLogger().RemoveSink(&logBuffer);
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
