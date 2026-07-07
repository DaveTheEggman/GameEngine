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

            // Camera: pulled back + up, looking at the fountain base.
            m_camera = m_scene->CreateEntity(u8"camera");
            m_scene->SetLocalPosition(m_camera, core::Vector3{ 0.0f, 7.0f, 22.0f });
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>()) {
                render::CameraComponent& cam = cameras->Add(m_camera);
                cam.clearColor = core::Color{ 0.02f, 0.02f, 0.04f, 1.0f };
            }
            m_fly.position = core::Vector3{ 0.0f, 7.0f, 22.0f };
            m_fly.pitch    = -0.28f;

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
                lc.color = core::Color{ 1.0f, 0.98f, 0.9f, 1.0f };
                lc.intensity = 2.5f;
            }

            BuildFountain(m_effect);

            if (auto* pmgr = m_scene->GetSystem<px::ParticleEffectComponentManager>()) {
                m_emitter = m_scene->CreateEntity(u8"fountain");
                m_scene->SetLocalPosition(m_emitter, core::Vector3{ 0.0f, 0.2f, 0.0f });
                px::ParticleEffectComponent& pc = pmgr->Add(m_emitter);
                pc.SetEffect(m_effect);   // no texture -> the renderer's default white (solid quads)
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
                }
                ImGui::TextDisabled("WASD/RMB fly; additive billboards");
            }
            ImGui::End();
        }

        scene::Scene*         m_scene = nullptr;
        scene::EntityHandle   m_camera;
        scene::EntityHandle   m_emitter;
        px::ParticleEffect    m_effect;
        samples::FlyCamera    m_fly;
        core::f32             m_frameSmooth = 0.016f;
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
