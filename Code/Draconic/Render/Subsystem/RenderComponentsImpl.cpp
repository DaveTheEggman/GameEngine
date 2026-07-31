// Draconic Render - render.subsystem implementation unit: component reflection bodies.
//
// Kept OUT of the :components interface partition (DRACONIC_REFLECT_* bodies make GCC emit a
// gcm cluster; see gcc-module-interface-hygiene). RenderComponents.cppm declares
// RegisterRenderComponentReflection(); this unit defines it + the DraconicRegister* bodies.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.render.subsystem;

import draconic.core;
import draconic.resource;
import draconic.scene;
import draconic.geometry;
import draconic.materials;
import draconic.rhi;

using namespace draconic::core;

// ============================================================================================
// Reflection (tooling: the editor inspector auto-generates property grids from these).
// Pointer/RefPtr/array fields (mesh, material, textures, bone matrices) are deliberately not
// reflected yet - they need resource-picker editors (editor phase 6). NON-export namespace:
// the macros expand static helpers (internal linkage), per the CoreReflection.cppm pattern.
// ============================================================================================
namespace draconic::render
{

    DRACONIC_REFLECT_ENUM(LightType, "draconic::render")
    {
        builder.Value("Directional", LightType::Directional);
        builder.Value("Point", LightType::Point);
        builder.Value("Spot", LightType::Spot);
    }

    DRACONIC_REFLECT_ENUM(ShadowUpdateMode, "draconic::render")
    {
        builder.Value("Realtime", ShadowUpdateMode::Realtime);
        builder.Value("Static", ShadowUpdateMode::Static);
    }

    DRACONIC_REFLECT_ENUM(ProbeUpdateMode, "draconic::render")
    {
        builder.Value("Static", ProbeUpdateMode::Static);
        builder.Value("Realtime", ProbeUpdateMode::Realtime);
        builder.Value("Manual", ProbeUpdateMode::Manual);
    }

    DRACONIC_REFLECT_VALUE(MeshComponent, "draconic::render")
    {
        builder
            .DataVersion(3) // v3: unified materials array (slot 0 = whole-mesh)
            .Property<&MeshComponent::mesh>("mesh")
            .Property<&MeshComponent::color>("color")
            .Property<&MeshComponent::visible>("visible");
    }

    DRACONIC_REFLECT_VALUE(InstancedMeshComponent, "draconic::render")
    {
        builder.Property<&InstancedMeshComponent::mesh>("mesh")
            .Property<&InstancedMeshComponent::material>("material")
            .Property<&InstancedMeshComponent::color>("color")
            .Property<&InstancedMeshComponent::visible>("visible");
    }

    DRACONIC_REFLECT_VALUE(CameraComponent, "draconic::render")
    {
        builder.Property<&CameraComponent::fovYRadians>("fovYRadians")
            .PropAttribute("displayName", String(u8"Field Of View"))
            .PropAttribute("description", String(u8"Vertical field of view (radians)"))
            .PropAttribute("range", Float4{0.10f, 3.04f, 0.01f, 0.0f})
            .Property<&CameraComponent::aspect>("aspect")
            .Property<&CameraComponent::nearZ>("nearZ")
            .Property<&CameraComponent::farZ>("farZ")
            .Property<&CameraComponent::clearColor>("clearColor")
            .Property<&CameraComponent::primary>("primary");
    }

    DRACONIC_REFLECT_VALUE(LightComponent, "draconic::render")
    {
        builder.Property<&LightComponent::type>("type")
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

    DRACONIC_REFLECT_ENUM(SpriteOrientation, "draconic::render")
    {
        builder.Value("CameraFacing", SpriteOrientation::CameraFacing);
        builder.Value("CameraFacingY", SpriteOrientation::CameraFacingY);
        builder.Value("WorldAligned", SpriteOrientation::WorldAligned);
    }

    DRACONIC_REFLECT_VALUE(SpriteComponent, "draconic::render")
    {
        builder.Property<&SpriteComponent::textureAsset>("texture")
            .Property<&SpriteComponent::size>("size")
            .Property<&SpriteComponent::uvRect>("uvRect")
            .Property<&SpriteComponent::tint>("tint")
            .Property<&SpriteComponent::orientation>("orientation")
            .Property<&SpriteComponent::additive>("additive")
            .Property<&SpriteComponent::visible>("visible");
    }

    DRACONIC_REFLECT_VALUE(DecalComponent, "draconic::render")
    {
        builder.Property<&DecalComponent::textureAsset>("texture")
            .Property<&DecalComponent::size>("size")
            .Property<&DecalComponent::color>("color")
            .Property<&DecalComponent::fadeStart>("fadeStart")
            .Property<&DecalComponent::fadeEnd>("fadeEnd")
            .Property<&DecalComponent::visible>("visible");
    }

    DRACONIC_REFLECT_ENUM(SkyMode, "draconic::render")
    {
        builder.Value("Procedural", SkyMode::Procedural);
        builder.Value("Analytic", SkyMode::Analytic);
        builder.Value("Color", SkyMode::Color);
        builder.Value("HDREquirect", SkyMode::HDREquirect);
        builder.Value("Cubemap", SkyMode::Cubemap);
    }

    DRACONIC_REFLECT_VALUE(EnvironmentSettings, "draconic::render")
    {
        builder
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

    DRACONIC_REFLECT_VALUE(ReflectionProbeComponent, "draconic::render")
    {
        builder.Property<&ReflectionProbeComponent::halfExtents>("halfExtents")
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

    DRACONIC_REFLECT_ENUM(TonemapOperator, "draconic::render")
    {
        builder.Value("Clamp", TonemapOperator::Clamp);
        builder.Value("AgX", TonemapOperator::AgX);
    }

    DRACONIC_REFLECT_ENUM(AaMode, "draconic::render")
    {
        builder.Value("Off", AaMode::Off);
        builder.Value("FXAA", AaMode::FXAA);
        builder.Value("TAA", AaMode::TAA);
    }

    DRACONIC_REFLECT_ENUM(AoMode, "draconic::render")
    {
        builder.Value("Off", AoMode::Off);
        builder.Value("GTAO", AoMode::GTAO);
        builder.Value("SSAO", AoMode::SSAO);
    }

    DRACONIC_REFLECT_VALUE(PostProcessSettings, "draconic::render")
    {
        builder.DataVersion(1)
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

} // namespace draconic::render (reflection bodies)

namespace draconic::render
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
} // namespace draconic::render
