// WebScene - the first FULL engine app running in a browser. Unlike WebTriangle (raw RHI + inline
// WGSL), this is a DefaultApplication: the whole subsystem stack (Scene/Render/Animation/Physics/
// Audio/Input/Script/UI) comes up, a scene of entities+components is built, and the renderer draws a
// lit, PBR-shaded spinning cube using the COOKED WGSL shader pack (shaders.dpak, bundled below).
// Same runtime path as the desktop Sandbox - just on the web runner + WebGPU. Proves "runs on web",
// not merely "compiles for web".
//
// Build (wasm preset) emits WebScene.html/.js/.wasm/.data; open the .html in a WebGPU browser.

#include "Draconic.Core/Prelude.h"

import draconic.core;
import draconic.runtime;
import draconic.runtime.client;
import draconic.shell;
import draconic.runtime.web;  // RunApplication (browser runner) - required by DRACONIC_APP_MAIN
import draconic.shell.web;    // WebShell - required by DRACONIC_APP_MAIN
import draconic.graphics;     // GraphicsDevice + FrameContext
import draconic.graphics.gpu; // CreateGraphicsDevice
import draconic.runtime.defaultapp; // DefaultApplication (registers the standard subsystems)
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;  // MeshComponent/CameraComponent/LightComponent + their managers
import draconic.geometry;          // Primitives::Cube / Plane
import draconic.materials;         // CreatePBR

#include "Runtime/Client/AppMain.h"
#include "../Common/FlyCamera.h" // shared free-fly camera (WASD/QE + RMB-look + orbit/pan/zoom)

namespace core = draconic::core;
namespace runtime = draconic::runtime;
namespace graphics = draconic::graphics;
namespace scene = draconic::scene;
namespace render = draconic::render;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;
namespace shell = draconic::shell;
namespace samples = draconic::samples;

namespace
{
    class WebSceneApp final : public runtime::DefaultApplication
    {
    public:
        void OnStartup(runtime::IApplicationHost& host) override
        {
            core::ConsoleWrite(u8"WebScene: building scene...\n");
            if (host.Ctx().GetSubsystem<scene::SceneSubsystem>() == nullptr)
            {
                core::ConsoleWrite(u8"WebScene: no scene subsystem.\n");
                return;
            }
            m_scene = PrimaryScenes().CreateScene(u8"web");

            // Ambient so the shadowed faces aren't pure black.
            if (auto* env = m_scene->GetSystem<render::EnvironmentSystem>())
            {
                env->Environment().ambientColor = core::Color{0.12f, 0.14f, 0.18f, 1.0f};
                env->Environment().ambientIntensity = 0.4f;
            }

            // The spinning PBR cube.
            if (auto* meshes = m_scene->GetSystem<render::MeshComponentManager>())
            {
                m_cube = m_scene->CreateEntity(u8"cube");
                render::MeshComponent& mc = meshes->Add(m_cube);
                mc.mesh = geometry::Primitives::Cube(1.0f);
                mc.SetMaterial(materials::CreatePBR(
                    u8"web.cube", core::Float4{0.85f, 0.35f, 0.28f, 1.0f}, 0.1f, 0.4f));

                // A ground plane under it.
                scene::EntityHandle ground = m_scene->CreateEntity(u8"ground");
                m_scene->SetLocalPosition(ground, core::Float3{0.0f, -0.75f, 0.0f});
                render::MeshComponent& gm = meshes->Add(ground);
                gm.mesh = geometry::Primitives::Plane(20.0f, 20.0f);
                gm.SetMaterial(materials::CreatePBR(
                    u8"web.ground", core::Float4{0.30f, 0.30f, 0.35f, 1.0f}, 0.0f, 0.8f));
            }

            // Directional key light.
            if (auto* lights = m_scene->GetSystem<render::LightComponentManager>())
            {
                scene::EntityHandle sun = m_scene->CreateEntity(u8"sun");
                core::Transform st = m_scene->GetLocalTransform(sun);
                st.rotation =
                    core::Quaternion::FromAxisAngle(core::Float3{1.0f, 0.0f, 0.0f}, -0.9f) *
                    core::Quaternion::FromAxisAngle(core::Float3{0.0f, 1.0f, 0.0f}, 0.5f);
                m_scene->SetLocalTransform(sun, st);
                render::LightComponent& sl = lights->Add(sun);
                sl.type = render::LightType::Directional;
                sl.color = core::Color{1.0f, 0.95f, 0.9f, 1.0f};
                sl.intensity = 2.0f;
            }

            // Static camera framing the cube.
            m_camera = m_scene->CreateEntity(u8"camera");
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>())
            {
                render::CameraComponent& cam = cameras->Add(m_camera);
                cam.fovYRadians = 1.04719755f; // 60 deg
                cam.nearZ = 0.1f;
                cam.farZ = 100.0f;
                cam.clearColor = core::Color{0.05f, 0.06f, 0.09f, 1.0f};
            }
            // Frame the cube; tune the fly speeds down for this 1-unit scene.
            m_fly.position = core::Float3{0.0f, 1.2f, 3.5f};
            m_fly.yaw = 0.0f;
            m_fly.pitch = -0.3f;
            m_fly.moveSpeed = 3.0f;
            m_fly.fastSpeed = 8.0f;
            m_fly.focusDistance = 3.5f;
            core::Transform ct = m_scene->GetLocalTransform(m_camera);
            ct.position = m_fly.position;
            ct.rotation = m_fly.Rotation();
            m_scene->SetLocalTransform(m_camera, ct);

            core::ConsoleWrite(u8"WebScene: started (WASD/QE move, hold right-mouse to look).\n");
        }

        void OnUpdate(runtime::IApplicationHost& host, core::f32 deltaTime) override
        {
            runtime::DefaultApplication::OnUpdate(host, deltaTime);
            if (m_scene == nullptr)
            {
                return;
            }

            // Spin the cube.
            m_spin += deltaTime;
            core::Transform ct = m_scene->GetLocalTransform(m_cube);
            ct.rotation = core::Quaternion::FromAxisAngle(core::Float3{0.0f, 1.0f, 0.0f}, m_spin);
            m_scene->SetLocalTransform(m_cube, ct);

            // Drive the camera from the shared free-fly helper (WASD/QE move, hold RMB to look) so the
            // web input path is exercised end-to-end and steerable on-screen.
            m_fly.Update(host, deltaTime);
            core::Transform camT = m_scene->GetLocalTransform(m_camera);
            camT.position = m_fly.position;
            camT.rotation = m_fly.Rotation();
            m_scene->SetLocalTransform(m_camera, camT);
        }

        void OnRenderWindow(runtime::IApplicationHost& host, graphics::FrameContext& frame) override
        {
            // The default render path reads the camera's aspect from the component; keep it synced.
            if (m_scene != nullptr && frame.height > 0)
            {
                if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>())
                {
                    if (render::CameraComponent* cam = cameras->Get(m_camera))
                    {
                        cam->aspect = static_cast<core::f32>(frame.width) /
                                      static_cast<core::f32>(frame.height);
                    }
                }
            }
            runtime::DefaultApplication::OnRenderWindow(host, frame);
        }

    private:
        scene::Scene* m_scene = nullptr;
        scene::EntityHandle m_cube{};
        scene::EntityHandle m_camera{};
        samples::FlyCamera m_fly;
        core::f32 m_spin = 0.0f;
    };
}

DRACONIC_APP_MAIN(WebSceneApp)
