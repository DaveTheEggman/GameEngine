// Draconic Render - render.subsystem implementation unit: component reflection bodies.
//
// Kept OUT of the :components interface partition (REFLECT_* bodies make GCC emit a
// gcm cluster; see gcc-module-interface-hygiene). RenderComponents.cppm declares
// RegisterRenderComponentReflection(); this unit defines it + the DraconicRegister* bodies.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.render;

import foundation.core;
import foundation.resource;
import foundation.scene;
import foundation.geometry;
import foundation.materials;
import foundation.rhi;
import foundation.script.facades; // ComponentOf<T> + RegisterExtra* (the script `.of` surface, Track A)

using namespace foundation::core;
using namespace foundation::render;
namespace core = foundation::core;

// ============================================================================================
// Reflection (tooling: the editor inspector auto-generates property grids from these).
// Pointer/RefPtr/array fields (mesh, material, textures, bone matrices) are deliberately not
// reflected yet - they need resource-picker editors (editor phase 6). NON-export namespace:
// the macros expand static helpers (internal linkage), per the CoreReflection.cppm pattern.
// ============================================================================================
namespace engine::render
{

    REFLECT_ENUM(LightType, "rtti::engine::render")
    {
        builder.Value("Directional", LightType::Directional);
        builder.Value("Point", LightType::Point);
        builder.Value("Spot", LightType::Spot);
    }

    REFLECT_ENUM(ShadowUpdateMode, "rtti::engine::render")
    {
        builder.Value("Realtime", ShadowUpdateMode::Realtime);
        builder.Value("Static", ShadowUpdateMode::Static);
    }

    REFLECT_ENUM(ProbeUpdateMode, "rtti::engine::render")
    {
        builder.Value("Static", ProbeUpdateMode::Static);
        builder.Value("Realtime", ProbeUpdateMode::Realtime);
        builder.Value("Manual", ProbeUpdateMode::Manual);
    }

    REFLECT_VALUE(MeshComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Mesh"))
            .Attribute("category", String(u8"Rendering"))
            .DataVersion(3) // v3: unified materials array (slot 0 = whole-mesh)
            // Script (Track A): MeshComponent.of(entity) -> a re-resolving handle; `color`/`visible`
            // set live from a behavior. `mesh`/`materials` are resource refs - swapped via the
            // resource-resolve primitive (Phase 1b), not this raw property.
            .Method<&foundation::script::ComponentOf<MeshComponent>, MeshComponent>("of")
            .Property<&MeshComponent::mesh>("mesh")
            .Property<&MeshComponent::color>("color")
            .Property<&MeshComponent::visible>("visible")
            // The material slots as a reflected container - the generic list editor renders it. The
            // description surfaces as the list's hover tooltip (the slot-0 / submesh semantics).
            .Nested<&MeshComponent::materials>("materials")
            .PropAttribute("description",
                           String(u8"Material slots, indexed by the mesh's submesh material index. "
                                  u8"Slot 0 also covers single-material meshes and any submesh whose "
                                  u8"index has no slot."));
    }

    REFLECT_VALUE(InstancedMeshComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Instanced Mesh"))
            .Attribute("category", String(u8"Rendering"))
            .Method<&foundation::script::ComponentOf<InstancedMeshComponent>, InstancedMeshComponent>(
                "of")
            .Property<&InstancedMeshComponent::mesh>("mesh")
            .Property<&InstancedMeshComponent::material>("material")
            .Property<&InstancedMeshComponent::color>("color")
            .Property<&InstancedMeshComponent::visible>("visible");
    }

    REFLECT_VALUE(CameraComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Camera"))
            .Attribute("category", String(u8"Rendering"))
            .Method<&foundation::script::ComponentOf<CameraComponent>, CameraComponent>("of")
            .Property<&CameraComponent::fovYRadians>("fovYRadians")
            .PropAttribute("displayName", String(u8"Field Of View"))
            .PropAttribute("description", String(u8"Vertical field of view (radians)"))
            .PropAttribute("range", Float4{0.10f, 3.04f, 0.01f, 0.0f})
            .Property<&CameraComponent::aspect>("aspect")
            .Property<&CameraComponent::nearZ>("nearZ")
            .Property<&CameraComponent::farZ>("farZ")
            .Property<&CameraComponent::clearColor>("clearColor")
            .Property<&CameraComponent::primary>("primary");
    }

    REFLECT_VALUE(LightComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Light"))
            .Attribute("category", String(u8"Rendering"))
            // Script (Track A): LightComponent.of(entity) -> live color/intensity/range/enabled/etc.
            .Method<&foundation::script::ComponentOf<LightComponent>, LightComponent>("of")
            .Property<&LightComponent::type>("type")
            .Property<&LightComponent::color>("color")
            .Property<&LightComponent::intensity>("intensity")
            .PropAttribute("range", Float4{0.0f, 50.0f, 0.1f, 0.0f})
            .Property<&LightComponent::range>("range")
            .PropAttribute("range", Float4{0.0f, 500.0f, 0.5f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"type=1,2"))
            .PropAttribute("description", String(u8"Falloff distance (point/spot lights)"))
            .Property<&LightComponent::innerAngle>("innerAngle")
            .PropAttribute("range", Float4{0.0f, 1.55f, 0.01f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"type=2"))
            .PropAttribute("description", String(u8"Spot cone inner half-angle (radians)"))
            .Property<&LightComponent::outerAngle>("outerAngle")
            .PropAttribute("range", Float4{0.0f, 1.55f, 0.01f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"type=2"))
            .PropAttribute("description", String(u8"Spot cone outer half-angle (radians)"))
            .Property<&LightComponent::shadowUpdate>("shadowUpdate")
            .PropAttribute("visibleWhen", String(u8"castsShadows"))
            .Property<&LightComponent::enabled>("enabled")
            .Property<&LightComponent::castsShadows>("castsShadows");
    }

    // The scene-bound render handle: SceneRender.of(scene).setMesh(entity, id) / setMaterial(...).
    // `of` returns SceneRender by value (concrete cross-backend return, no ReturnType-override), like
    // ScenePhysics. setMesh/setMaterial are world ops keyed by entity that swap a resource::Ref by id.
    REFLECT_VALUE(SceneRender, "rtti::engine::render")
    {
        builder.Method<&SceneRender::setMesh>("setMesh", {"entity", "resourceId"});
        builder.Method<&SceneRender::setMaterial>("setMaterial", {"entity", "resourceId"});
        builder.Method<&SceneRender::of>("of", {"scene"});
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    REFLECT_ENUM(SpriteOrientation, "rtti::engine::render")
    {
        builder.Value("CameraFacing", SpriteOrientation::CameraFacing);
        builder.Value("CameraFacingY", SpriteOrientation::CameraFacingY);
        builder.Value("WorldAligned", SpriteOrientation::WorldAligned);
    }

    REFLECT_VALUE(SpriteComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Sprite"))
            .Attribute("category", String(u8"Rendering"))
            .Method<&foundation::script::ComponentOf<SpriteComponent>, SpriteComponent>("of")
            .Property<&SpriteComponent::textureAsset>("texture")
            .Property<&SpriteComponent::size>("size")
            .Property<&SpriteComponent::uvRect>("uvRect")
            .Property<&SpriteComponent::tint>("tint")
            .Property<&SpriteComponent::orientation>("orientation")
            .Property<&SpriteComponent::additive>("additive")
            .Property<&SpriteComponent::visible>("visible");
    }

    REFLECT_VALUE(DecalComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Decal"))
            .Attribute("category", String(u8"Rendering"))
            .Method<&foundation::script::ComponentOf<DecalComponent>, DecalComponent>("of")
            .Property<&DecalComponent::textureAsset>("texture")
            .Property<&DecalComponent::size>("size")
            .Property<&DecalComponent::color>("color")
            .Property<&DecalComponent::fadeStart>("fadeStart")
            .Property<&DecalComponent::fadeEnd>("fadeEnd")
            .Property<&DecalComponent::visible>("visible");
    }

    REFLECT_ENUM(SkyMode, "rtti::engine::render")
    {
        builder.Value("Procedural", SkyMode::Procedural);
        builder.Value("Analytic", SkyMode::Analytic);
        builder.Value("Color", SkyMode::Color);
        builder.Value("HDREquirect", SkyMode::HDREquirect);
        builder.Value("Cubemap", SkyMode::Cubemap);
    }

    REFLECT_VALUE(EnvironmentSettings, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Environment"))
            .Attribute("category", String(u8"Rendering"))
            // Script (Track A): EnvironmentSettings.of(scene) -> the scene's LIVE environment (edit
            // ambient/sky/fog fields). A scene-scoped re-resolving handle (the settings are one-per-scene).
            .Method<&EnvironmentSettingsOf, EnvironmentSettings>("of")
            .DataVersion(
                3) // v3: skyBackgroundIntensity (visible-backdrop dimmer, separate from IBL)
            .Property<&EnvironmentSettings::ambientColor>("ambientColor")
            .Property<&EnvironmentSettings::ambientIntensity>("ambientIntensity")
            .PropAttribute("range", Float4{0.0f, 2.0f, 0.01f, 0.0f})
            .PropAttribute(
                "description",
                String(
                    u8"Flat ambient fill added on top of the image-based ambient (0 = pure IBL)"))
            .Property<&EnvironmentSettings::skyMode>("skyMode")
            .Property<&EnvironmentSettings::skyTexture>("skyTexture")
            .PropAttribute("visibleWhen", String(u8"skyMode=3,4"))
            .PropAttribute("description",
                           String(u8"HDR (equirect) or cube texture for the textured sky modes"))
            .Property<&EnvironmentSettings::skyIntensity>("skyIntensity")
            .PropAttribute("range", Float4{0.0f, 10.0f, 0.05f, 0.0f})
            .PropAttribute(
                "description",
                String(u8"Environment radiance master: scales the sky AND the IBL lighting"))
            .Property<&EnvironmentSettings::skyBackgroundIntensity>("skyBackgroundIntensity")
            .PropAttribute("range", Float4{0.0f, 4.0f, 0.05f, 0.0f})
            .PropAttribute("displayName", String(u8"Sky Background Intensity"))
            .PropAttribute("description",
                           String(u8"Dims only the VISIBLE sky backdrop; leaves the IBL lighting"))
            .Property<&EnvironmentSettings::skyRotation>("skyRotation")
            .PropAttribute("range", Float4{0.0f, 6.2832f, 0.01f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"skyMode=3,4"))
            .PropAttribute("description", String(u8"Sky yaw (radians)"))
            .Property<&EnvironmentSettings::skyHorizon>("skyHorizon")
            .PropAttribute("visibleWhen", String(u8"skyMode=0"))
            .Property<&EnvironmentSettings::skyZenith>("skyZenith")
            .PropAttribute("visibleWhen", String(u8"skyMode=0,2"))
            .PropAttribute("displayName", String(u8"Sky Zenith / Color"))
            .PropAttribute("description",
                           String(u8"Zenith color (procedural sky); the flat color in Color mode"))
            .Property<&EnvironmentSettings::skyGround>("skyGround")
            .PropAttribute("visibleWhen", String(u8"skyMode=0"))
            .Property<&EnvironmentSettings::sunIntensity>("sunIntensity")
            .PropAttribute("range", Float4{0.0f, 10.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"skyMode=0,1,2"))
            .Property<&EnvironmentSettings::sunAngularSize>("sunAngularSize")
            .PropAttribute("range", Float4{0.05f, 10.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"skyMode=0,1,2"))
            .PropAttribute("description", String(u8"Sun disc size (degrees)"))
            .Property<&EnvironmentSettings::turbidity>("turbidity")
            .PropAttribute("range", Float4{2.0f, 10.0f, 0.1f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"skyMode=1"))
            .PropAttribute("description", String(u8"Preetham haze (2 = clear, 10 = hazy)"));
    }

    REFLECT_VALUE(ReflectionProbeComponent, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Reflection Probe"))
            .Attribute("category", String(u8"Rendering"))
            .Method<&foundation::script::ComponentOf<ReflectionProbeComponent>, ReflectionProbeComponent>(
                "of")
            .Property<&ReflectionProbeComponent::halfExtents>("halfExtents")
            .Property<&ReflectionProbeComponent::blendDistance>("blendDistance")
            .PropAttribute("range", Float4{0.0f, 10.0f, 0.1f, 0.0f})
            .PropAttribute("description", String(u8"Fade width at the probe volume's edge"))
            .Property<&ReflectionProbeComponent::intensity>("intensity")
            .PropAttribute("range", Float4{0.0f, 5.0f, 0.05f, 0.0f})
            .Property<&ReflectionProbeComponent::resolution>("resolution")
            .Property<&ReflectionProbeComponent::priority>("priority")
            .Property<&ReflectionProbeComponent::update>("update")
            .Property<&ReflectionProbeComponent::parallax>("parallax")
            .Property<&ReflectionProbeComponent::enabled>("enabled");
    }

    REFLECT_ENUM(TonemapOperator, "rtti::engine::render")
    {
        builder.Value("Clamp", TonemapOperator::Clamp);
        builder.Value("AgX", TonemapOperator::AgX);
    }

    REFLECT_ENUM(AaMode, "rtti::engine::render")
    {
        builder.Value("Off", AaMode::Off);
        builder.Value("FXAA", AaMode::FXAA);
        builder.Value("TAA", AaMode::TAA);
    }

    REFLECT_ENUM(AoMode, "rtti::engine::render")
    {
        builder.Value("Off", AoMode::Off);
        builder.Value("GTAO", AoMode::GTAO);
        builder.Value("SSAO", AoMode::SSAO);
    }

    REFLECT_VALUE(PostProcessSettings, "rtti::engine::render")
    {
        builder.Attribute("displayName", String(u8"Post Processing"))
            .Attribute("category", String(u8"Rendering")).DataVersion(1)
            // Script (Track A): PostProcessSettings.of(scene) -> the scene's LIVE post settings (edit
            // exposure/tonemap/bloom/AA). A scene-scoped re-resolving handle.
            .Method<&PostProcessSettingsOf, PostProcessSettings>("of")
            .Property<&PostProcessSettings::exposureEV>("exposureEV")
            .PropAttribute("range", Float4{-8.0f, 8.0f, 0.05f, 0.0f})
            .PropAttribute("displayName", String(u8"Exposure (EV)"))
            .PropAttribute("description",
                           String(u8"Exposure in stops; the tonemap applies 2^EV (0 = neutral)"))
            .Property<&PostProcessSettings::tonemapOperator>("tonemapOperator")
            .PropAttribute("displayName", String(u8"Tonemap"))
            .Property<&PostProcessSettings::bloomEnabled>("bloomEnabled")
            .PropAttribute("displayName", String(u8"Bloom"))
            .Property<&PostProcessSettings::bloomThreshold>("bloomThreshold")
            .PropAttribute("range", Float4{0.0f, 4.0f, 0.01f, 0.0f})
            .Property<&PostProcessSettings::bloomKnee>("bloomKnee")
            .PropAttribute("range", Float4{0.0f, 1.0f, 0.01f, 0.0f})
            .Property<&PostProcessSettings::bloomIntensity>("bloomIntensity")
            .PropAttribute("range", Float4{0.0f, 1.0f, 0.005f, 0.0f})
            .Property<&PostProcessSettings::aoMode>("aoMode")
            .PropAttribute("displayName", String(u8"Ambient Occlusion"))
            .Property<&PostProcessSettings::aoStrength>("aoStrength")
            .PropAttribute("range", Float4{0.0f, 1.0f, 0.01f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aoMode=1,2"))
            .PropAttribute("description", String(u8"Master AO mix (0 = none, 1 = full)"))
            .Property<&PostProcessSettings::aoRadius>("aoRadius")
            .PropAttribute("range", Float4{0.05f, 4.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aoMode=1,2"))
            .Property<&PostProcessSettings::aoIntensity>("aoIntensity")
            .PropAttribute("range", Float4{0.0f, 4.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aoMode=1,2"))
            .Property<&PostProcessSettings::ssrEnabled>("ssrEnabled")
            .PropAttribute("displayName", String(u8"Screen-Space Reflections"))
            .Property<&PostProcessSettings::ssrIntensity>("ssrIntensity")
            .PropAttribute("range", Float4{0.0f, 2.0f, 0.02f, 0.0f})
            .Property<&PostProcessSettings::aaMode>("aaMode")
            .PropAttribute("displayName", String(u8"Anti-Aliasing"))
            .Property<&PostProcessSettings::taaBlendFactor>("taaBlendFactor")
            .PropAttribute("range", Float4{0.5f, 0.99f, 0.005f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aaMode=2"))
            .PropAttribute("description",
                           String(u8"TAA history weight (higher = steadier, more ghosting)"))
            .Property<&PostProcessSettings::taaVarianceGamma>("taaVarianceGamma")
            .PropAttribute("range", Float4{0.5f, 3.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aaMode=2"))
            .Property<&PostProcessSettings::fxaaSubpixel>("fxaaSubpixel")
            .PropAttribute("range", Float4{0.0f, 1.0f, 0.05f, 0.0f})
            .PropAttribute("visibleWhen", String(u8"aaMode=1"));
    }

} // namespace engine::render (reflection bodies)

namespace engine::render
{
    void RegisterRenderComponentReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterEnum_LightType();
            DraconicRegisterEnum_SpriteOrientation();
            DraconicRegisterEnum_ShadowUpdateMode();
            DraconicRegisterEnum_ProbeUpdateMode();
            DraconicRegisterEnum_SkyMode();
            DraconicRegisterValue_EnvironmentSettings();
            DraconicRegisterEnum_TonemapOperator();
            DraconicRegisterEnum_AaMode();
            DraconicRegisterEnum_AoMode();
            DraconicRegisterValue_PostProcessSettings();
            RegisterArrayType<foundation::resource::Ref<foundation::materials::Material>>();
            DraconicRegisterValue_MeshComponent();
            DraconicRegisterValue_InstancedMeshComponent();
            DraconicRegisterValue_CameraComponent();
            DraconicRegisterValue_LightComponent();
            DraconicRegisterValue_SpriteComponent();
            DraconicRegisterValue_DecalComponent();
            DraconicRegisterValue_ReflectionProbeComponent();
            return true;
        }();
        (void)once;
    }

    void RegisterRenderScriptFacade()
    {
        RegisterRenderComponentReflection(); // ensure component TypeData (incl `of`) is built first
        // Surface the render COMPONENTS to script (Track A: MeshComponent.of(entity), ...): register
        // them (both backends emit registry types), seed the Wren emission roots (nothing else
        // reaches an of()-only type), and make their class names import-visible in behavior preludes.
        // The WHOLE render component set - each `.of(entity)` exposes its editor-reflected properties.
        struct RenderComponentEntry
        {
            const core::TypeInfo* type;
            core::StringView name;
        };
        const RenderComponentEntry components[] = {
            {&core::TypeOf<MeshComponent>(), u8"MeshComponent"},
            {&core::TypeOf<InstancedMeshComponent>(), u8"InstancedMeshComponent"},
            {&core::TypeOf<CameraComponent>(), u8"CameraComponent"},
            {&core::TypeOf<LightComponent>(), u8"LightComponent"},
            {&core::TypeOf<SpriteComponent>(), u8"SpriteComponent"},
            {&core::TypeOf<DecalComponent>(), u8"DecalComponent"},
            {&core::TypeOf<ReflectionProbeComponent>(), u8"ReflectionProbeComponent"}};
        for (const RenderComponentEntry& component : components)
        {
            GlobalTypeRegistry().Register(*component.type);
            foundation::script::RegisterExtraScriptRootType(component.type);
            foundation::script::RegisterExtraFacadeName(component.name);
        }

        // The scene-bound render handle (SceneRender.of(scene)): reflect it, register it, seed the
        // Wren emission root (nothing else reaches it), and make the class name prelude-visible.
        DraconicRegisterValue_SceneRender();
        GlobalTypeRegistry().Register(core::TypeOf<SceneRender>());
        foundation::script::RegisterExtraScriptRootType(&core::TypeOf<SceneRender>());
        foundation::script::RegisterExtraFacadeName(u8"SceneRender");

        // The render scene-SYSTEM settings handles (EnvironmentSettings.of(scene) / PostProcess-
        // Settings.of(scene)): register + seed the Wren emission root + name for the prelude. Their
        // TypeData (incl `of`) was built by RegisterRenderComponentReflection above.
        const core::TypeInfo* settings[] = {&core::TypeOf<EnvironmentSettings>(),
                                            &core::TypeOf<PostProcessSettings>()};
        const core::StringView settingsNames[] = {u8"EnvironmentSettings", u8"PostProcessSettings"};
        for (core::usize i = 0; i < 2; ++i)
        {
            GlobalTypeRegistry().Register(*settings[i]);
            foundation::script::RegisterExtraScriptRootType(settings[i]);
            foundation::script::RegisterExtraFacadeName(settingsNames[i]);
        }
    }
} // namespace engine::render
