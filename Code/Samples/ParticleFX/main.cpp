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

            const core::Vector3 obstacle = CellPos(6) + core::Vector3{ 0.0f, 1.4f, 0.0f };
            const core::f32     obRadius = 1.4f;
            BuildFountain(m_effect);
            BuildMeshShards(m_debris);      // cell 1: opaque material (set below)
            BuildMeshShards(m_meshGlow);    // cell 2: additive material (set below) - identical sim
            BuildEmbers(m_embers);
            BuildHaze(m_haze);
            BuildTrail(m_trail);
            BuildCollision(m_collide, obstacle, obRadius);
            BuildLocal(m_local);
            BuildSmoke(m_smoke);
            BuildFire(m_fire);
            BuildCampfire(m_campfire);
            BuildFireworks(m_fireworks);

            if (auto* pmgr = m_scene->GetSystem<px::ParticleEffectComponentManager>()) {
                // One system per cell of a 4x4 showcase grid (cells 12-15 reserved for later samples).
                // Cell 0: billboard fountain (additive soft dots).
                m_emitter = m_scene->CreateEntity(u8"fountain");
                m_scene->SetLocalPosition(m_emitter, CellPos(0));
                pmgr->Add(m_emitter).SetEffect(m_effect);   // no texture -> the renderer's soft-dot default

                // Cells 1 & 2: two mesh systems side by side, IDENTICAL sim - only the material differs.
                // Cell 1 = opaque solid shards; cell 2 = additive glow (routed to the Transparent pass).
                m_debrisEmitter = m_scene->CreateEntity(u8"shards-solid");
                m_scene->SetLocalPosition(m_debrisEmitter, CellPos(1));
                px::ParticleEffectComponent& dc = pmgr->Add(m_debrisEmitter);
                dc.SetEffect(m_debris);
                dc.mesh      = geometry::Primitives::Cube(1.0f);
                dc.material  = materials::CreatePBR(u8"shards-solid", core::Vector4{ 0.35f, 0.6f, 0.9f, 1.0f }, 0.1f, 0.5f);
                dc.meshScale = 0.5f;

                m_glowEmitter = m_scene->CreateEntity(u8"shards-glow");
                m_scene->SetLocalPosition(m_glowEmitter, CellPos(2));
                px::ParticleEffectComponent& gc = pmgr->Add(m_glowEmitter);
                gc.SetEffect(m_meshGlow);
                gc.mesh      = geometry::Primitives::Cube(1.0f);
                gc.meshScale = 0.5f;
                {
                    // Additive PBR material -> the extractor routes these to the Transparent pass.
                    auto glow = materials::CreatePBR(u8"shards-glow", core::Vector4{ 0.4f, 0.8f, 1.0f, 1.0f }, 0.0f, 0.4f);
                    glow->pipeline.blendMode = materials::BlendMode::Additive;
                    glow->pipeline.depthMode = materials::DepthMode::ReadOnly;
                    gc.material = glow;
                }

                // Cell 3: light particles (drifting embers) - a point light per particle + a billboard glow.
                m_emberEmitter = m_scene->CreateEntity(u8"embers");
                m_scene->SetLocalPosition(m_emberEmitter, CellPos(3));
                px::ParticleEffectComponent& ec = pmgr->Add(m_emberEmitter);
                ec.SetEffect(m_embers);
                ec.lightIntensity = 14.0f;
                ec.lightRange     = 8.0f;

                // Cell 4: ground haze - the soft-particle A/B showcase.
                m_hazeEmitter = m_scene->CreateEntity(u8"haze");
                m_scene->SetLocalPosition(m_hazeEmitter, CellPos(4) + core::Vector3{ 0.0f, 0.4f, 0.0f });
                pmgr->Add(m_hazeEmitter).SetEffect(m_haze);

                // Cell 5: trail sparks (camera-facing ribbons).
                m_trailEmitter = m_scene->CreateEntity(u8"trail-sparks");
                m_scene->SetLocalPosition(m_trailEmitter, CellPos(5));
                pmgr->Add(m_trailEmitter).SetEffect(m_trail);

                // Cell 6: collision rain bouncing off a rendered sphere obstacle + the world ground.
                m_collideEmitter = m_scene->CreateEntity(u8"rain");
                m_scene->SetLocalPosition(m_collideEmitter, CellPos(6) + core::Vector3{ 0.0f, 6.0f, 0.0f });
                pmgr->Add(m_collideEmitter).SetEffect(m_collide);
                if (auto* meshes = m_scene->GetSystem<render::MeshComponentManager>()) {
                    scene::EntityHandle ob = m_scene->CreateEntity(u8"obstacle");
                    m_scene->SetLocalPosition(ob, obstacle);
                    render::MeshComponent& omc = meshes->Add(ob);
                    omc.mesh = geometry::Primitives::Sphere(obRadius);
                    omc.material = materials::CreatePBR(u8"obstacle", core::Vector4{ 0.7f, 0.7f, 0.72f, 1.0f }, 0.1f, 0.4f);
                }

                // Cell 7: local-space puff - orbits its emitter (animated in OnUpdate); cloud follows rigidly.
                m_localEmitter = m_scene->CreateEntity(u8"orbit");
                m_scene->SetLocalPosition(m_localEmitter, CellPos(7));
                pmgr->Add(m_localEmitter).SetEffect(m_local);

                // Cell 8: smoke. Cell 9: fire. Cell 10: campfire (fire + smoke). Cell 11: fireworks.
                m_smokeEmitter = m_scene->CreateEntity(u8"smoke");
                m_scene->SetLocalPosition(m_smokeEmitter, CellPos(8));
                pmgr->Add(m_smokeEmitter).SetEffect(m_smoke);

                m_fireEmitter = m_scene->CreateEntity(u8"fire");
                m_scene->SetLocalPosition(m_fireEmitter, CellPos(9));
                pmgr->Add(m_fireEmitter).SetEffect(m_fire);

                m_campfireEmitter = m_scene->CreateEntity(u8"campfire");
                m_scene->SetLocalPosition(m_campfireEmitter, CellPos(10));
                pmgr->Add(m_campfireEmitter).SetEffect(m_campfire);

                m_fireworksEmitter = m_scene->CreateEntity(u8"fireworks");
                m_scene->SetLocalPosition(m_fireworksEmitter, CellPos(11));
                pmgr->Add(m_fireworksEmitter).SetEffect(m_fireworks);

                ApplySoft(m_effect); ApplySoft(m_embers); ApplySoft(m_haze); ApplySoft(m_smoke);
                ApplySoft(m_campfire);   // sync soft systems to the slider
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

                // Orbit the local-space emitter so its (Local) cloud visibly rides along as a rigid body.
                m_orbitTime += deltaTime;
                const core::Vector3 c = CellPos(7);
                m_scene->SetLocalPosition(m_localEmitter,
                    c + core::Vector3{ 2.5f * core::Cos(m_orbitTime * 1.5f), 1.5f, 2.5f * core::Sin(m_orbitTime * 1.5f) });
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

        // Collision showcase: rain that spawns high and bounces off both the world ground plane (y=0) and a
        // rendered sphere obstacle at `obstacle` (radius `obRadius`). World-space, so the collider centre
        // is a world position matching the drawn sphere.
        static void BuildCollision(px::ParticleEffect& effect, core::Vector3 obstacle, core::f32 obRadius)
        {
            px::ParticleSystem& sys = effect.AddSystem(4000);
            sys.name       = core::String{ u8"rain" };
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.blendMode  = px::ParticleBlendMode::Alpha;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 500.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Box(core::Vector3{ 3.0f, 0.1f, 3.0f });
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(3.0f, 4.0f);
            sys.AddInitializer<px::VelocityInitializer>().baseVelocity = core::Vector3{ 0.0f, -1.0f, 0.0f };
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.12f, 0.12f });
            sys.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 0.5f, 0.75f, 1.0f, 0.9f });
            sys.AddBehavior<px::GravityBehavior>().multiplier = 1.0f;
            {
                px::CollisionBehavior& col = sys.AddBehavior<px::CollisionBehavior>();
                col.planes[0]  = px::CollisionPlane{ core::Vector3{ 0.0f, 1.0f, 0.0f }, 0.0f };   // world ground
                col.planeCount = 1;
                col.spheres[0]  = px::CollisionSphere{ obstacle, obRadius };                       // the drawn obstacle
                col.sphereCount = 1;
                col.radius       = 0.06f;   // particle radius so drops sit on the surface, not in it
                col.bounce       = 0.45f;
                col.friction     = 0.15f;
                col.lifetimeLoss = 0.2f;    // lose some life on each hit so splashes settle
            }
        }

        // A single fire system (billboard additive): fast rising, shrinking, colour-graded white->red.
        static void ConfigureFire(px::ParticleSystem& sys, core::f32 scale)
        {
            sys.name       = core::String{ u8"fire" };
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 160.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Circle(0.6f * scale);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(0.6f, 1.1f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 3.2f * scale, 0.0f };
                v.randomness   = core::Vector3{ 0.7f, 0.6f, 0.7f };
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 1.0f * scale, 1.0f * scale });
            sys.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 1.0f, 0.9f, 0.5f, 1.0f });
            sys.AddBehavior<px::TurbulenceBehavior>().strength = 1.5f;   // flicker/curl
            sys.AddBehavior<px::SizeOverLifetimeBehavior>().curve =
                px::ParticleCurveVector2::Linear(core::Vector2{ 1.0f * scale, 1.0f * scale }, core::Vector2{ 0.15f * scale, 0.15f * scale });
            // Colour ramp: white-hot -> yellow -> orange -> red, fading out at the tip.
            px::ParticleCurveColor ramp;
            ramp.AddKey(0.0f, core::Vector4{ 1.0f, 0.95f, 0.7f, 1.0f });
            ramp.AddKey(0.35f, core::Vector4{ 1.0f, 0.6f, 0.2f, 0.9f });
            ramp.AddKey(0.7f, core::Vector4{ 0.9f, 0.2f, 0.05f, 0.5f });
            ramp.AddKey(1.0f, core::Vector4{ 0.4f, 0.05f, 0.02f, 0.0f });
            sys.AddBehavior<px::ColorOverLifetimeBehavior>().curve = ramp;
        }

        // A single smoke system (billboard alpha, soft): slow rise, expanding, drifting, fading.
        static void ConfigureSmoke(px::ParticleSystem& sys, core::f32 scale, core::f32 rise)
        {
            sys.name       = core::String{ u8"smoke" };
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.blendMode  = px::ParticleBlendMode::Alpha;
            sys.softParticles = true; sys.softDistance = 1.5f;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 24.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Circle(0.5f * scale);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(3.0f, 5.0f);
            sys.AddInitializer<px::VelocityInitializer>().baseVelocity = core::Vector3{ 0.0f, rise, 0.0f };
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.8f * scale, 0.8f * scale });
            // Light, ambient-lit grey so it reads against the dark scene (dark grey alpha over a dark floor
            // is nearly invisible); the alpha curve does the fade-in/out.
            sys.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 0.55f, 0.55f, 0.6f, 0.9f });
            sys.AddBehavior<px::TurbulenceBehavior>().strength = 0.8f;   // lazy drift
            sys.AddBehavior<px::DragBehavior>().drag = 0.4f;
            sys.AddBehavior<px::SizeOverLifetimeBehavior>().curve =
                px::ParticleCurveVector2::Linear(core::Vector2{ 0.8f * scale, 0.8f * scale }, core::Vector2{ 3.0f * scale, 3.0f * scale });
            // Fade in from nothing, hold, fade out (billows appear then dissipate).
            px::ParticleCurveFloat a;
            a.AddKey(0.0f, 0.0f); a.AddKey(0.2f, 0.6f); a.AddKey(0.7f, 0.4f); a.AddKey(1.0f, 0.0f);
            sys.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = a;
        }

        static void BuildFire(px::ParticleEffect& effect)  { ConfigureFire(effect.AddSystem(2000), 1.0f); }
        static void BuildSmoke(px::ParticleEffect& effect) { ConfigureSmoke(effect.AddSystem(1200), 1.0f, 1.6f); }

        // Composite effect: fire at the base + smoke rising above it, in ONE effect (two systems) - the
        // multi-system authoring case.
        static void BuildCampfire(px::ParticleEffect& effect)
        {
            ConfigureFire(effect.AddSystem(2000), 0.7f);
            ConfigureSmoke(effect.AddSystem(800), 0.7f, 2.2f);
        }

        // Fireworks: rockets shoot up and, on death, burst into a colour-inheriting spark shower via a
        // sub-emitter link (OnDeath -> child system, inherit position + colour).
        static void BuildFireworks(px::ParticleEffect& effect)
        {
            // System 0: rockets - launch up, short life, additive with a trail.
            px::ParticleSystem& rocket = effect.AddSystem(64);
            rocket.name       = core::String{ u8"rocket" };
            rocket.renderMode = px::ParticleRenderMode::Trail;
            rocket.blendMode  = px::ParticleBlendMode::Additive;
            rocket.emitter.mode = px::EmissionMode::Continuous;
            rocket.emitter.spawnRate = 3.0f;   // a few launches per second
            rocket.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Circle(0.4f);
            rocket.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.0f, 1.4f);
            {
                px::VelocityInitializer& v = rocket.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 13.0f, 0.0f };
                v.randomness   = core::Vector3{ 1.5f, 1.5f, 1.5f };
            }
            rocket.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.25f, 0.25f });
            // Vivid warm hues (gold -> hot magenta) so the bursts pop against the dark sky - blues would
            // wash out. Sparks inherit this colour via the sub-emitter link.
            rocket.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 1.0f, 0.85f, 0.2f, 1.0f }, core::Vector4{ 1.0f, 0.25f, 0.7f, 1.0f });
            rocket.AddBehavior<px::GravityBehavior>().multiplier = 1.0f;   // arc to an apex, then die
            rocket.trail.enabled = true; rocket.trail.maxPoints = 20; rocket.trail.lifetime = 0.4f;
            rocket.trail.widthStart = 0.12f; rocket.trail.widthEnd = 0.0f; rocket.trail.recordInterval = 0.02f;

            // System 1: sparks - spawned by rocket deaths (no self-emission); explode outward, gravity, fade.
            px::ParticleSystem& sparks = effect.AddSystem(6000);
            sparks.name       = core::String{ u8"sparks" };
            sparks.renderMode = px::ParticleRenderMode::Billboard;
            sparks.blendMode  = px::ParticleBlendMode::Additive;
            sparks.emitter.isEmitting = false;
            {
                px::PositionInitializer& p = sparks.AddInitializer<px::PositionInitializer>();
                p.shape = px::EmissionShape::Point();
            }
            sparks.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(0.9f, 1.7f);
            {
                px::VelocityInitializer& v = sparks.AddInitializer<px::VelocityInitializer>();
                v.shape = px::EmissionShape::Sphere(1.0f, /*shell*/ true);   // radial burst
                v.shapeDirectionSpeed = 7.0f;
                v.randomness = core::Vector3{ 1.0f, 1.0f, 1.0f };
            }
            sparks.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.16f, 0.16f });
            sparks.AddInitializer<px::ColorInitializer>().color = px::RangeColor::Constant(core::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
            sparks.AddBehavior<px::GravityBehavior>().multiplier = 1.3f;
            sparks.AddBehavior<px::DragBehavior>().drag = 0.6f;
            sparks.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = px::ParticleCurveFloat::FadeOut(1.0f, 0.15f);

            px::SubEmitterLink link = px::SubEmitterLink::Default();
            link.trigger          = px::ParticleEventType::OnDeath;
            link.childSystemIndex = 1;
            link.spawnCount       = 80;
            link.probability      = 1.0f;
            link.inheritPosition  = true;
            link.inheritColor     = true;   // sparks take the rocket's colour (needs the SpawnAt inherit fix)
            effect.AddSubEmitterLink(link);
        }

        // Local-space showcase: a tight puff that rigidly follows its orbiting emitter (see OnUpdate).
        static void BuildLocal(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(2000);
            sys.name       = core::String{ u8"orbit" };
            sys.renderMode = px::ParticleRenderMode::Billboard;
            sys.blendMode  = px::ParticleBlendMode::Additive;
            sys.simulationSpace = px::ParticleSpace::Local;   // cloud moves as a rigid body with the emitter
            sys.prewarmTime = 1.5f;                           // start already-populated
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 200.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(0.3f);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.0f, 1.6f);
            sys.AddInitializer<px::VelocityInitializer>().baseVelocity = core::Vector3{ 0.0f, 0.4f, 0.0f };
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.18f, 0.18f });
            sys.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 1.0f, 0.8f, 0.3f, 1.0f }, core::Vector4{ 1.0f, 0.4f, 0.1f, 1.0f });
            sys.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = px::ParticleCurveFloat::FadeOut(1.0f, 0.2f);
        }

        // Shared mesh-particle config: tumbling shards that rise and spread. Used for BOTH mesh cells -
        // identical simulation; the only difference is the component's material (opaque solid vs. additive
        // glow, which routes to the Transparent pass). Demonstrates the mesh blend->category path.
        static void BuildMeshShards(px::ParticleEffect& effect)
        {
            px::ParticleSystem& sys = effect.AddSystem(2000);
            sys.name       = core::String{ u8"shards" };
            sys.renderMode = px::ParticleRenderMode::Mesh;
            sys.emitter.mode = px::EmissionMode::Continuous;
            sys.emitter.spawnRate = 80.0f;

            sys.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(0.4f);
            sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.6f, 2.6f);
            {
                px::VelocityInitializer& v = sys.AddInitializer<px::VelocityInitializer>();
                v.baseVelocity = core::Vector3{ 0.0f, 3.5f, 0.0f };
                v.randomness   = core::Vector3{ 1.5f, 1.0f, 1.5f };
            }
            sys.AddInitializer<px::SizeInitializer>().size = px::RangeVector2::Constant(core::Vector2{ 0.3f, 0.3f });
            sys.AddInitializer<px::ColorInitializer>().color =
                px::RangeColor(core::Vector4{ 0.3f, 0.9f, 1.0f, 1.0f }, core::Vector4{ 0.5f, 0.3f, 1.0f, 1.0f });
            sys.AddInitializer<px::RotationInitializer>();
            sys.AddInitializer<px::MeshOrientationInitializer>();
            sys.AddBehavior<px::RotationOverLifetimeBehavior>();                 // tumble
            sys.AddBehavior<px::DragBehavior>().drag = 0.5f;
            sys.AddBehavior<px::AlphaOverLifetimeBehavior>().curve = px::ParticleCurveFloat::FadeOut(1.0f, 0.1f);
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
                if (changed) { ApplySoft(m_effect); ApplySoft(m_embers); ApplySoft(m_haze); ApplySoft(m_smoke); ApplySoft(m_campfire); }
                ImGui::TextDisabled("WASD/RMB fly. 4x4 grid: fountain/mesh solid/mesh glow/embers");
                ImGui::TextDisabled("haze/trail/collision/orbit/smoke/fire/campfire/fireworks");
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
        scene::EntityHandle   m_collideEmitter;
        scene::EntityHandle   m_localEmitter;
        scene::EntityHandle   m_glowEmitter;
        scene::EntityHandle   m_smokeEmitter;
        scene::EntityHandle   m_fireEmitter;
        scene::EntityHandle   m_campfireEmitter;
        scene::EntityHandle   m_fireworksEmitter;
        px::ParticleEffect    m_effect;
        px::ParticleEffect    m_debris;
        px::ParticleEffect    m_embers;
        px::ParticleEffect    m_haze;
        px::ParticleEffect    m_trail;
        px::ParticleEffect    m_collide;
        px::ParticleEffect    m_local;
        px::ParticleEffect    m_meshGlow;
        px::ParticleEffect    m_smoke;
        px::ParticleEffect    m_fire;
        px::ParticleEffect    m_campfire;
        px::ParticleEffect    m_fireworks;
        samples::FlyCamera    m_fly;
        core::f32             m_frameSmooth = 0.016f;
        core::f32             m_orbitTime = 0.0f;      // drives the local-space emitter orbit
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
