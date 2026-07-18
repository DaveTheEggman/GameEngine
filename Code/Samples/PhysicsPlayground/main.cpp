// PhysicsPlayground - the physics P1 consumer proof: a stack of falling crates, a
// kinematic sweeper, a trigger volume, and crosshair raycast shoving - all authored as
// RigidBody/Collider COMPONENTS on a scene, simulated by the Jolt-backed subsystem on the
// engine's fixed lane with render interpolation, and drawn via the physics DEBUG
// wireframes (green = awake dynamic, grey = sleeping, blue = static/kinematic, yellow =
// trigger). Fly with WASD/RMB-look; LEFT-CLICK shoves the body under the crosshair; R
// respawns the stack; the ImGui panel has gravity + time-scale sliders and live counts.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"
#include "imgui.h"
#include <cmath>

import draconic.core;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.defaultapp;
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.imgui;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.subsystem;

#include "../Common/FlyCamera.h"   // after the imports: uses draconic::core/runtime types

namespace core = draconic::core;
namespace runtime = draconic::runtime;
namespace graphics = draconic::graphics;
namespace shell = draconic::shell;
namespace dscene = draconic::scene;
namespace render = draconic::render;
namespace physics = draconic::physics;
namespace imgui = draconic::imgui;

using core::f32;

namespace
{
    class PlaygroundApp final : public runtime::DefaultApplication
    {
    public:
        void Configure(runtime::IApplicationHost& host) override
        {
            runtime::DefaultApplication::Configure(host);
            host.Ctx().AddSubsystem<physics::PhysicsSubsystem>();
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                host.Ctx().AddSubsystem<imgui::ImguiSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
            }
        }

        void OnLaunch(runtime::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<dscene::SceneSubsystem>();
            if (scenes == nullptr) { return; }
            m_scene = scenes->CreateScene(u8"playground");
            m_physics = m_scene->GetSystem<physics::PhysicsSceneSystem>();
            if (m_physics != nullptr) { m_physics->Settings().debugDraw = true; }

            // Camera.
            m_camera = m_scene->CreateEntity(u8"camera");
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>())
            {
                cameras->Add(m_camera);
            }
            m_fly.position = core::Float3{ 8.0f, 6.0f, 14.0f };
            m_fly.yaw = 0.5f;
            m_fly.pitch = -0.3f;
            m_fly.moveSpeed = 10.0f;
            m_fly.fastSpeed = 30.0f;

            BuildWorld();
            m_scene->Start();
            m_scene->SetSimulationEnabled(true);
            core::ConsoleWrite(u8"PhysicsPlayground: WASD/RMB-look fly, LMB shove, R respawn.\n");
        }

        void OnUpdate(runtime::IApplicationHost& host, f32 dt) override
        {
            runtime::DefaultApplication::OnUpdate(host, dt);
            m_fly.Update(host, dt);
            PushCameraToEntity();

            auto* input = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            if (input == nullptr) { return; }
            if (input->Keyboard()->IsKeyPressed(shell::KeyCode::Escape)) { host.RequestExit(0); }
            if (input->Keyboard()->IsKeyPressed(shell::KeyCode::R)) { RespawnStack(); }

            // Arrow-key character drive (world axes) + Space jump.
            if (auto* characters = m_scene->GetSystem<physics::CharacterComponentManager>())
            {
                if (physics::CharacterComponent* hero = characters->Get(m_hero))
                {
                    const f32 speed = 4.0f;
                    core::Float3 move{ 0.0f, 0.0f, 0.0f };
                    if (input->Keyboard()->IsKeyDown(shell::KeyCode::Right)) { move.x += speed; }
                    if (input->Keyboard()->IsKeyDown(shell::KeyCode::Left)) { move.x -= speed; }
                    if (input->Keyboard()->IsKeyDown(shell::KeyCode::Up)) { move.z -= speed; }
                    if (input->Keyboard()->IsKeyDown(shell::KeyCode::Down)) { move.z += speed; }
                    hero->moveVelocity = move;
                    if (input->Keyboard()->IsKeyPressed(shell::KeyCode::Space)) { hero->jumpSpeed = 6.0f; }
                }
            }

            // Crosshair shove: ray along the camera forward; impulse along the ray.
            if (input->Mouse()->IsButtonPressed(shell::MouseButton::Left)
                && m_physics != nullptr && m_physics->World() != nullptr)
            {
                physics::RayHit hit;
                const core::Float3 forward = m_fly.Forward();
                if (m_physics->World()->RayCast(m_fly.position, forward, 200.0f, hit))
                {
                    m_physics->World()->AddImpulse(hit.body,
                        core::Float3{ forward.x * 400.0f, forward.y * 400.0f + 120.0f,
                                      forward.z * 400.0f });
                    m_lastSurface = hit.surface;   // ramp panels report 1 / 2
                    m_haveSurface = true;
                }
            }

            // Trigger events -> console (the volume floats over the stack).
            if (m_physics != nullptr)
            {
                for (const physics::ContactEvent& e : m_physics->Events())
                {
                    if (e.kind == physics::ContactKind::TriggerEnter)
                    {
                        core::ConsoleWrite(u8"PhysicsPlayground: trigger entered!\n");
                    }
                }
            }

            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>())
            {
                g->NewFrame(input, dt);
                DrawHud(host);
            }
        }

        void OnRenderWindow(runtime::IApplicationHost& host, graphics::FrameContext& frame) override
        {
            if (m_scene != nullptr && frame.height > 0)
            {
                if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>())
                {
                    if (render::CameraComponent* cam = cameras->Get(m_camera))
                    {
                        cam->aspect = static_cast<f32>(frame.width) / static_cast<f32>(frame.height);
                    }
                }
            }
            runtime::DefaultApplication::OnRenderWindow(host, frame);
            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>()) { g->Render(frame); }
        }

    private:
        void BuildWorld()
        {
            auto* bodies = m_scene->GetSystem<physics::RigidBodyComponentManager>();

            // Ground: an infinite plane (P2) - crates land anywhere, not just on a slab.
            {
                dscene::EntityHandle e = m_scene->CreateEntity(u8"ground");
                physics::RigidBodyComponent& body = bodies->Add(e);
                body.motion = physics::MotionKind::Static;
                body.layer = physics::PhysicsLayer::Static;
                body.shape = physics::ShapeKind::Plane;
                body.planeHalfExtent = 200.0f;
            }
            // A cooked TRIANGLE-MESH ramp (P2): two panels, two material slots - the
            // crosshair ray reports which slot it hit (HUD "surface").
            {
                const core::Float3 positions[] = {
                    { 6.0f, 0.0f, -3.0f }, { 6.0f, 0.0f, 3.0f },      // low edge
                    { 12.0f, 3.0f, 3.0f }, { 12.0f, 3.0f, -3.0f },    // high edge
                    { 18.0f, 3.0f, 3.0f }, { 18.0f, 3.0f, -3.0f },    // flat top end
                };
                const core::u32 indices[] = { 0, 1, 2, 0, 2, 3,       // sloped panel
                                              3, 2, 4, 3, 4, 5 };     // flat panel
                const core::u32 slots[] = { 1, 1, 2, 2 };
                core::Array<core::byte> blob;
                if (physics::CookTriangleMesh(
                        core::Span<const core::Float3>(positions, 6),
                        core::Span<const core::u32>(indices, 12),
                        core::Span<const core::u32>(slots, 4), blob))
                {
                    m_rampShape = core::MakeRef<physics::CollisionShape>(core::DefaultAllocator());
                    m_rampShape->blob.Resize(blob.Size());
                    core::MemCopy(m_rampShape->blob.Data(), blob.Data(), blob.Size());
                    core::Array<core::Float3> outline;
                    if (physics::ExtractShapeTriangles(
                            core::Span<const core::byte>(blob.Data(), blob.Size()), outline))
                    {
                        m_rampShape->outline = static_cast<core::Array<core::Float3>&&>(outline);
                    }
                    dscene::EntityHandle e = m_scene->CreateEntity(u8"ramp");
                    physics::RigidBodyComponent& body = bodies->Add(e);
                    body.motion = physics::MotionKind::Static;
                    body.layer = physics::PhysicsLayer::Static;
                    body.shape = physics::ShapeKind::Cooked;
                    body.collisionShape = m_rampShape;
                }
            }
            // A cooked CONVEX boulder (P2) dropped onto the ramp - hulls may be dynamic.
            {
                core::Array<core::Float3> points;
                const core::f32 axes[3][3] = { { 0.9f, 0, 0 }, { 0, 0.7f, 0 }, { 0, 0, 0.8f } };
                for (const auto& a : axes)
                {
                    points.PushBack(core::Float3{ a[0], a[1], a[2] });
                    points.PushBack(core::Float3{ -a[0], -a[1], -a[2] });
                }
                points.PushBack(core::Float3{ 0.5f, 0.5f, 0.5f });
                points.PushBack(core::Float3{ -0.5f, 0.5f, -0.5f });
                core::Array<core::byte> blob;
                if (physics::CookConvexHull(
                        core::Span<const core::Float3>(points.Data(), points.Size()), blob))
                {
                    m_boulderShape = core::MakeRef<physics::CollisionShape>(core::DefaultAllocator());
                    m_boulderShape->blob.Resize(blob.Size());
                    core::MemCopy(m_boulderShape->blob.Data(), blob.Data(), blob.Size());
                    core::Array<core::Float3> outline;
                    if (physics::ExtractShapeTriangles(
                            core::Span<const core::byte>(blob.Data(), blob.Size()), outline))
                    {
                        m_boulderShape->outline = static_cast<core::Array<core::Float3>&&>(outline);
                    }
                    m_boulder = m_scene->CreateEntity(u8"boulder");
                    m_scene->SetLocalPosition(m_boulder, core::Float3{ 14.0f, 8.0f, 0.0f });
                    physics::RigidBodyComponent& body = bodies->Add(m_boulder);
                    body.shape = physics::ShapeKind::Cooked;
                    body.collisionShape = m_boulderShape;
                    body.friction = 0.4f;
                }
            }
            // The crate stack (5x4 wall).
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 5; ++col)
                {
                    dscene::EntityHandle e = m_scene->CreateEntity(u8"crate");
                    m_scene->SetLocalPosition(e,
                        core::Float3{ (col - 2) * 1.05f, 0.5f + row * 1.05f, 0.0f });
                    physics::RigidBodyComponent& body = bodies->Add(e);
                    body.halfExtents = core::Float3{ 0.5f, 0.5f, 0.5f };
                    body.friction = 0.6f;
                    m_crates.PushBack(e);
                }
            }
            // The character (P3): arrow keys drive it, Space jumps; it climbs the ramp
            // (stairs + slopes) and shoves crates with its 500N of push.
            {
                m_hero = m_scene->CreateEntity(u8"hero");
                m_scene->SetLocalPosition(m_hero, core::Float3{ -6.0f, 0.9f, 4.0f });
                m_scene->GetSystem<physics::CharacterComponentManager>()->Add(m_hero);
            }
            // A motorized hinge spinner (P3 joints): a blade welded to the world pivot,
            // spinning at 2 rad/s - walk the character into it to get batted away.
            {
                dscene::EntityHandle e = m_scene->CreateEntity(u8"spinner");
                m_scene->SetLocalPosition(e, core::Float3{ -6.0f, 1.0f, -4.0f });
                physics::RigidBodyComponent& body = bodies->Add(e);
                body.halfExtents = core::Float3{ 2.0f, 0.1f, 0.1f };
                physics::JointComponent& joint =
                    m_scene->GetSystem<physics::JointComponentManager>()->Add(e);
                joint.kind = physics::JointKind::Hinge;
                joint.localAxis = core::Float3{ 0.0f, 1.0f, 0.0f };
                joint.motorEnabled = true;
                joint.motorTargetVelocity = 2.0f;
            }
            // A kinematic sweeper the update drives in a circle (knocks crates around).
            {
                m_sweeper = m_scene->CreateEntity(u8"sweeper");
                m_scene->SetLocalPosition(m_sweeper, core::Float3{ 6.0f, 0.75f, 0.0f });
                physics::RigidBodyComponent& body = bodies->Add(m_sweeper);
                body.motion = physics::MotionKind::Kinematic;
                body.layer = physics::PhysicsLayer::Kinematic;
                body.halfExtents = core::Float3{ 0.4f, 0.75f, 0.4f };
            }
            // The trigger volume above the stack.
            {
                dscene::EntityHandle e = m_scene->CreateEntity(u8"trigger");
                m_scene->SetLocalPosition(e, core::Float3{ 0.0f, 6.0f, 0.0f });
                physics::RigidBodyComponent& body = bodies->Add(e);
                body.motion = physics::MotionKind::Kinematic;
                body.isTrigger = true;
                body.halfExtents = core::Float3{ 2.0f, 1.0f, 2.0f };
            }
        }

        void RespawnStack()
        {
            // Teleport the crates back into the wall formation (velocities cleared).
            if (m_physics == nullptr || m_physics->World() == nullptr) { return; }
            auto* bodies = m_scene->GetSystem<physics::RigidBodyComponentManager>();
            int i = 0;
            for (dscene::EntityHandle e : m_crates)
            {
                const int row = i / 5;
                const int col = i % 5;
                ++i;
                physics::RigidBodyComponent* body = bodies->Get(e);
                if (body == nullptr || !body->body.IsValid()) { continue; }
                const core::Float3 position{ (col - 2) * 1.05f, 0.5f + row * 1.05f, 0.0f };
                m_physics->World()->SetBodyTransform(body->body, position,
                                                     core::Quaternion::Identity);
                m_physics->World()->SetLinearVelocity(body->body, core::Float3{ 0, 0, 0 });
                body->prevPosition = body->currPosition = position;
                body->prevRotation = body->currRotation = core::Quaternion::Identity;
            }
        }

        void DrawHud(runtime::IApplicationHost& host)
        {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("Physics");
            if (m_physics != nullptr && m_physics->World() != nullptr)
            {
                ImGui::Text("bodies: %zu", m_physics->World()->BodyCount());
                core::Float3 gravity = m_physics->World()->Gravity();
                if (ImGui::SliderFloat("gravity y", &gravity.y, -30.0f, 10.0f))
                {
                    m_physics->World()->SetGravity(gravity);
                }
            }
            float scale = host.Ctx().TimeScale();
            if (ImGui::SliderFloat("time scale", &scale, 0.0f, 2.0f))
            {
                host.Ctx().SetTimeScale(scale);
            }
            if (m_haveSurface) { ImGui::Text("last hit surface slot: %u", m_lastSurface); }
            if (auto* characters = m_scene->GetSystem<physics::CharacterComponentManager>())
            {
                if (physics::CharacterComponent* hero = characters->Get(m_hero))
                {
                    ImGui::Text("character: %s", hero->ground == physics::CharacterGround::OnGround
                                                     ? "grounded" : "airborne");
                }
            }
            ImGui::Text("LMB shove | R respawn | arrows+Space character | Esc quit");
            ImGui::End();

            // Drive the sweeper in a circle through SCENE transforms (kinematic follow).
            m_sweepAngle += 0.6f * (1.0f / 60.0f) * host.Ctx().TimeScale();
            if (m_sweeper.IsAssigned())
            {
                core::Transform t = m_scene->GetLocalTransform(m_sweeper);
                t.position = core::Float3{ 6.0f * std::cos(m_sweepAngle), 0.75f,
                                           6.0f * std::sin(m_sweepAngle) };
                m_scene->SetLocalTransform(m_sweeper, t);
            }
        }

        void PushCameraToEntity()
        {
            core::Transform t = m_scene->GetLocalTransform(m_camera);
            t.position = m_fly.position;
            t.rotation = m_fly.Rotation();
            m_scene->SetLocalTransform(m_camera, t);
        }

        dscene::Scene* m_scene = nullptr;
        physics::PhysicsSceneSystem* m_physics = nullptr;
        dscene::EntityHandle m_camera;
        dscene::EntityHandle m_sweeper;
        core::Array<dscene::EntityHandle> m_crates;
        dscene::EntityHandle m_boulder;
        dscene::EntityHandle m_hero;
        core::RefPtr<physics::CollisionShape> m_rampShape;
        core::RefPtr<physics::CollisionShape> m_boulderShape;
        core::u32 m_lastSurface = 0;
        bool m_haveSurface = false;
        draconic::samples::FlyCamera m_fly;
        f32 m_sweepAngle = 0.0f;
    };
}

DRACONIC_APP_MAIN(PlaygroundApp)
