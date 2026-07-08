// ParticleFX - the particle-system showcase. Builds a ParticleEffect in code (an additive fountain),
// attaches it to an entity via ParticleEffectComponent, and lets draconic.particles.subsystem tick the
// CPU sim + draw the billboards through the dedicated ParticleRenderer. Phase 2 of the particle track
// (docs/design/particles.md): CPU sim on the existing extract->resolve->draw pipeline. GPU-compute sim,
// trails, mesh particles, and the cooked resource/editor land in later phases.

#include "Core/Prelude.h"
#include "imgui.h"

import draconic.core;
import draconic.rhi;
import draconic.runtime;
import draconic.runtime.client;
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime.defaultapp;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.render;
import draconic.imgui;
import draconic.geometry;
import draconic.materials;
import draconic.particles;             // the CPU sim (effect/system/modules)
import draconic.particles.subsystem;   // the ECS component + ParticleSubsystem

#include "../Common/FlyCamera.h"

namespace core = draconic::core;
namespace samples = draconic::samples;
namespace rhi = draconic::rhi;
namespace runtime = draconic::runtime;
namespace graphics = draconic::graphics;
namespace shell = draconic::shell;
namespace scene = draconic::scene;
namespace render = draconic::render;
namespace imgui = draconic::imgui;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;
namespace px = draconic::particles;

namespace
{
    class ParticleFXApp final : public runtime::DefaultApplication
    {
    public:
        void Configure(runtime::IApplicationHost& host) override
        {
            runtime::DefaultApplication::Configure(host);
            host.Ctx().AddSubsystem<px::ParticleSubsystem>();
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) {
                host.Ctx().AddSubsystem<imgui::ImguiSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
            }
        }

        void OnStartup(runtime::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<scene::SceneSubsystem>();
            if (scenes == nullptr) { return; }
            m_scene = scenes->CreateScene(u8"particlefx");

            // Dim cool ambient so the (unlit) additive particles pop against the lit floor.
            if (auto* env = m_scene->GetSystem<render::EnvironmentSystem>()) {
                env->Environment().ambientColor     = core::Color{ 0.10f, 0.12f, 0.18f, 1.0f };
                env->Environment().ambientIntensity = 0.4f;
            }

            // Camera: high + pulled back to frame the whole 4x4 showcase grid.
            m_camera = m_scene->CreateEntity(u8"camera");
            m_scene->SetLocalPosition(m_camera, core::Vector3{ 0.0f, 22.0f, 34.0f });
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>()) {
                render::CameraComponent& cam = cameras->Add(m_camera);
                cam.clearColor = core::Color{ 0.02f, 0.02f, 0.04f, 1.0f };
            }
            m_fly.position = core::Vector3{ 0.0f, 22.0f, 34.0f };
            m_fly.pitch    = -0.5f;

            // A floor for spatial context.
            if (auto* meshes = m_scene->GetSystem<render::MeshComponentManager>()) {
                scene::EntityHandle floor = m_scene->CreateEntity(u8"floor");
                m_scene->SetLocalPosition(floor, core::Vector3{ 0.0f, 0.0f, 0.0f });
                render::MeshComponent& mc = meshes->Add(floor);
                mc.mesh = geometry::Primitives::Plane(80.0f, 80.0f);
                mc.material = materials::CreatePBR(u8"floor", core::Vector4{ 0.20f, 0.22f, 0.26f, 1.0f }, 0.0f, 0.8f);
            }
            if (auto* lights = m_scene->GetSystem<render::LightComponentManager>()) {
                scene::EntityHandle key = m_scene->CreateEntity(u8"key");
                core::Transform kt = m_scene->GetLocalTransform(key);
                kt.rotation = core::Quaternion::FromAxisAngle(core::Vector3{ 1.0f, 0.0f, 0.0f }, -0.9f);
                m_scene->SetLocalTransform(key, kt);
                render::LightComponent& lc = lights->Add(key);
                lc.type = render::LightType::Directional;
                lc.color = core::Color{ 0.6f, 0.7f, 0.95f, 1.0f };   // cool + dim so the warm ember point-lights read
                lc.intensity = 1.0f;
            }

            BuildFountain(m_effect);
            BuildDebris(m_debris);
            BuildEmbers(m_embers);
            BuildHaze(m_haze);
            BuildTrail(m_trail);

            if (auto* pmgr = m_scene->GetSystem<px::ParticleEffectComponentManager>()) {
                // One system per cell of a 4x4 showcase grid (11 cells reserved for later samples).
                // Cell 0: billboard fountain (additive soft dots).
                m_emitter = m_scene->CreateEntity(u8"fountain");
                m_scene->SetLocalPosition(m_emitter, CellPos(0));
                pmgr->Add(m_emitter).SetEffect(m_effect);   // no texture -> the renderer's soft-dot default

                // Cell 1: mesh-particle debris (tumbling cubes through the instanced-mesh path).
                m_debrisEmitter = m_scene->CreateEntity(u8"debris");
                m_scene->SetLocalPosition(m_debrisEmitter, CellPos(1));
                px::ParticleEffectComponent& dc = pmgr->Add(m_debrisEmitter);
                dc.SetEffect(m_debris);
                dc.mesh     = geometry::Primitives::Cube(1.0f);
                dc.material = materials::CreatePBR(u8"debris", core::Vector4{ 0.75f, 0.5f, 0.28f, 1.0f }, 0.15f, 0.6f);
                dc.meshScale = 1.0f;

                // Cell 2: light particles (drifting embers) - a point light per particle + a billboard glow.
                m_emberEmitter = m_scene->CreateEntity(u8"embers");
                m_scene->SetLocalPosition(m_emberEmitter, CellPos(2));
                px::ParticleEffectComponent& ec = pmgr->Add(m_emberEmitter);
                ec.SetEffect(m_embers);
                ec.lightIntensity = 14.0f;
                ec.lightRange     = 8.0f;

                // Cell 3: ground haze - the soft-particle A/B showcase.
                m_hazeEmitter = m_scene->CreateEntity(u8"haze");
                m_scene->SetLocalPosition(m_hazeEmitter, CellPos(3) + core::Vector3{ 0.0f, 0.4f, 0.0f });
                pmgr->Add(m_hazeEmitter).SetEffect(m_haze);

                // Cell 4: trail sparks (camera-facing ribbons).
                m_trailEmitter = m_scene->CreateEntity(u8"sparks");
                m_scene->SetLocalPosition(m_trailEmitter, CellPos(4));
                pmgr->Add(m_trailEmitter).SetEffect(m_trail);

                ApplySoft(m_effect); ApplySoft(m_embers); ApplySoft(m_haze);   // sync all systems to the slider
            }
        }

        void OnRenderWindow(runtime::IApplicationHost& host, graphics::FrameContext& frame) override
        {
            runtime::DefaultApplication::OnRenderWindow(host, frame);
            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>()) { g->Render(frame); }
        }

        void OnUpdate(runtime::IApplicationHost& host, core::f32 deltaTime) override
        {
            runtime::DefaultApplication::OnUpdate(host, deltaTime);
            m_frameSmooth = m_frameSmooth * 0.9f + deltaTime * 0.1f;

            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>()) {
                g->NewFrame(host.Shell() != nullptr ? host.Shell()->Input() : nullptr, deltaTime);
                BuildHud();
            }
            m_fly.Update(host, deltaTime);
            if (m_scene != nullptr) {
                core::Transform camT = m_scene->GetLocalTransform(m_camera);
                camT.position = m_fly.position;
                camT.rotation = m_fly.Rotation();
                m_scene->SetLocalTransform(m_camera, camT);
            }
        }

    private:
        // 4x4 showcase grid, centered on the origin. Cells are indexed row-major (0..15); each holds one
        // particle system. Returns the cell's floor-level center (callers add any per-system y offset).
        static constexpr core::f32 kCellSpacing = 10.0f;
        static constexpr core::i32 kGridCols    = 4;
        static core::Vector3 CellPos(core::i32 index)
        {
            const core::f32 half = (kGridCols - 1) * 0.5f;
            const core::f32 col  = static_cast<core::f32>(index % kGridCols);
            const core::f32 row  = static_cast<core::f32>(index / kGridCols);
            return core::Vector3{ (col - half) * kCellSpacing, 0.2f, (row - half) * kCellSpacing };
        }

        static void BuildFountain(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(30000);
            sys.name       = core::String{ u8"fountain" };
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 3000.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(0.25f);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.6f, 2.6f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 13.0f, 0.0f };
                v.randomness   = core::Vector3{ 3.0f, 2.0f, 3.0f };
                v.shape        = px::EmissionShape::Cone(0.4f, 0.35f);
                v.shapeDirectionSpeed = 4.0f;
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.35f, 0.35f });
            sys.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 1.0f, 0.55f, 0.15f, 1.0f }, core::Vector4{ 1.0f, 0.85f, 0.4f, 1.0f });

            sys.AddBehavior<px::GravityBehavior>().multiplier = 1.4f;
            sys.AddBehavior<px::DragBehavior>().drag = 0.25f;
            sys.AddBehavior<px::ColorOverLifetimeBehavior>().curve =
                px::ParticleCurveColor::FadeAlpha(core::Vector4{ 1.0f, 0.5f, 0.12f, 1.0f }, 0.35f);
            sys.AddBehavior<px::SizeOverLifetimeBehavior>().curve =
                px::ParticleCurveVector2::Linear(core::Vector2{ 0.4f, 0.4f }, core::Vector2{ 0.04f, 0.04f });
        }

        // Mesh-mode particles: tumbling cubes that arc up and fall, drawn through the instanced-mesh path.
        static void BuildDebris(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(3000);
            sys.name       = core::String{ u8"debris" };
            sys.renderMode = px::ParticleRenderMode::Mesh;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 120.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(0.3f);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(2.0f, 3.5f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 9.0f, 0.0f };
                v.randomness   = core::Vector3{ 5.0f, 2.0f, 5.0f };
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.35f, 0.35f });
            sys.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 0.85f, 0.85f, 0.9f, 1.0f });
            sys.AddInitializer<px::RotationInitializer>();         // random start angle + spin speed
            sys.AddInitializer<px::MeshOrientationInitializer>();  // random tumble axis

            sys.AddBehavior<px::GravityBehavior>().multiplier = 1.6f;
            sys.AddBehavior<px::RotationOverLifetimeBehavior>();   // inactive curve -> advances angle by spin speed (tumble)
        }

        // Light-mode particles: slow warm embers drifting up; each contributes a point light so they
        // paint moving pools of light on the floor (plus the additive billboard glow).
        static void BuildEmbers(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(400);   // few: capped to the light budget
            sys.name       = core::String{ u8"embers" };
            sys.renderMode = px::ParticleRenderMode::Light;
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 40.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Box(core::Vector3{ 4.0f, 0.1f, 4.0f });
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(2.5f, 4.5f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 1.4f, 0.0f };
                v.randomness   = core::Vector3{ 0.5f, 0.3f, 0.5f };
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.6f, 0.6f });
            sys.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 1.0f, 0.5f, 0.15f, 1.0f }, core::Vector4{ 1.0f, 0.75f, 0.3f, 1.0f });
            sys.AddBehavior<px::DragBehavior>().drag = 0.5f;
            sys.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = px::ParticleCurveFloat::FadeOut(1.0f, 0.55f);  // bright, then fade
        }

        // Ground haze: big, slow, camera-facing billboards centered at floor level so each quad straddles
        // the ground plane - the clearest soft-particle A/B (hard clip line vs. soft fade at the floor).
        static void BuildHaze(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(300);
            sys.name       = core::String{ u8"haze" };
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.softDistance = 2.0f;   // wide fade band, obvious in the A/B
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 14.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Box(core::Vector3{ 3.5f, 0.05f, 3.5f });
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(4.0f, 6.0f);
            sys.AddInitializer<px::VelocityInitializer>().baseVelocity = core::Vector3{ 0.0f, 0.25f, 0.0f };
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 2.0f, 2.0f });  // straddles the floor for the A/B
            sys.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 0.35f, 0.28f, 0.45f, 1.0f });
            sys.AddBehavior<px::DragBehavior>().drag = 0.6f;
            sys.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = px::ParticleCurveFloat::FadeOut(1.0f, 0.4f);
        }

        // Trail-mode: arcing sparks, each leaving a camera-facing ribbon behind it (the Phase-5 showcase).
        static void BuildTrail(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(2000);
            sys.name       = core::String{ u8"sparks" };
            sys.renderMode = px::ParticleRenderMode::Trail;
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 40.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(0.15f);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.4f, 2.4f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 8.0f, 0.0f };
                v.randomness   = core::Vector3{ 6.0f, 3.0f, 6.0f };   // spray sideways so the ribbons curve
                v.shape        = px::EmissionShape::Cone(0.5f, 0.2f);
                v.shapeDirectionSpeed = 3.0f;
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.2f, 0.2f });
            sys.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 0.2f, 0.7f, 1.0f, 1.0f }, core::Vector4{ 0.9f, 0.4f, 1.0f, 1.0f });

            sys.AddBehavior<px::GravityBehavior>().multiplier = 1.5f;   // arc back down -> curved ribbons
            sys.AddBehavior<px::ColorOverLifetimeBehavior>().curve =
                px::ParticleCurveColor::FadeAlpha(core::Vector4{ 0.5f, 0.6f, 1.0f, 1.0f }, 0.3f);

            sys.trail.enabled          = true;
            sys.trail.maxPoints        = 32;
            sys.trail.recordInterval   = 0.02f;
            sys.trail.lifetime         = 0.6f;    // how long each recorded point lingers (ribbon length)
            sys.trail.widthStart       = 0.22f;
            sys.trail.widthEnd         = 0.0f;    // taper to a point at the tail
            sys.trail.minVertexDistance = 0.04f;
            sys.trail.useParticleColor = true;
        }

        // Push the current soft-particle distance to every system in an effect (0 => disabled).
        void ApplySoft(px::ParticleEffect& fx)
        {
            for (core::i32 i = 0; i < fx.SystemCount(); ++i) {
                if (px::ParticleSystem* s = fx.GetSystem(i)) { s->softParticles = m_softOn; s->softDistance = m_softDistance; }
            }
        }

        void BuildHud()
        {
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("ParticleFX")) {
                px::ParticleSystem* sys = m_effect.GetSystem(0);
                ImGui::Text("alive: %d", sys != nullptr ? sys->AliveCount() : 0);
                ImGui::Text("fps: %.0f", 1.0f / core::Max(m_frameSmooth, 0.0001f));
                if (sys != nullptr) {
                    bool emit = sys->emitter.isEmitting;
                    if (ImGui::Checkbox("emit", &emit)) { sys->emitter.isEmitting = emit; }
                    ImGui::SliderFloat("rate", &sys->emitter.spawnRate, 0.0f, 12000.0f, "%.0f/s");
                    // Fountain blend mode - live (read at extract), so all four PSO variants are eyeballable.
                    const char* kBlends[] = { "Alpha", "Additive", "Premultiplied", "Multiply" };
                    int blend = static_cast<int>(sys->blendMode);
                    if (ImGui::Combo("blend", &blend, kBlends, 4)) { sys->blendMode = static_cast<px::ParticleBlendMode>(blend); }
                }
                // Soft-particle A/B (live - read every frame at extract). Checkbox = on/off; slider tunes
                // the fade band (kept while off, so toggling restores it). Applies to all billboard systems.
                bool changed = ImGui::Checkbox("soft particles", &m_softOn);
                changed |= ImGui::SliderFloat("soft dist", &m_softDistance, 0.0f, 4.0f, "%.2f");
                if (changed) { ApplySoft(m_effect); ApplySoft(m_embers); ApplySoft(m_haze); }
                ImGui::TextDisabled("WASD/RMB fly; toggle soft + watch the haze meet the floor");
            }
            ImGui::End();
        }

        scene::Scene*         m_scene = nullptr;
        scene::EntityHandle   m_camera;
        scene::EntityHandle   m_emitter;
        scene::EntityHandle   m_debrisEmitter;
        scene::EntityHandle   m_emberEmitter;
        scene::EntityHandle   m_hazeEmitter;
        scene::EntityHandle   m_trailEmitter;
        px::ParticleEffect    m_effect;
        px::ParticleEffect    m_debris;
        px::ParticleEffect    m_embers;
        px::ParticleEffect    m_haze;
        px::ParticleEffect    m_trail;
        samples::FlyCamera    m_fly;
        core::f32             m_frameSmooth = 0.016f;
        bool                  m_softOn = true;         // soft-particle on/off (HUD checkbox)
        core::f32             m_softDistance = 2.0f;   // soft-particle fade band (HUD slider)
    };
}

int main(int, char**)
{
    auto shell = shell::CreateShell();
    graphics::GraphicsDeviceDesc gpuDesc{};
    auto gpu = graphics::CreateGraphicsDevice(gpuDesc);
    graphics::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    ParticleFXApp app;
    return runtime::RunApplication(app, *shell, device);
}
