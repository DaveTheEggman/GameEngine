// draconic.particles.subsystem:components - the ECS bridge between draconic.particles (the CPU
// sim) and the renderer. Ported in spirit from Sedulous's ParticleComponent(Manager).
//
//   ParticleEffectComponent        - attaches a ParticleEffect to an entity; owns its runtime instance.
//   ParticleEffectComponentManager - a SceneSystem that (a) ticks every instance in PostUpdate
//                                    (scene-driven sim) and (b) packs live billboards into
//                                    ParticleBillboardRenderData during render extraction (the
//                                    IRenderDataProvider role - called by the ParticleSubsystem).

module;
#include "Core/Prelude.h"

export module draconic.particles.subsystem:components;

import draconic.core;
import draconic.rhi;                 // TextureView (billboard texture)
import draconic.scene;              // Scene, ComponentManager, EntityHandle, ScenePhase
import draconic.render;             // ExtractedScene, RenderCategories, MultiMeshRenderData
import draconic.geometry;           // StaticMesh (mesh-mode particles)
import draconic.materials;          // Material (mesh-mode particles)
import draconic.particles;          // ParticleEffect / ParticleEffectInstance / ParticleSystem
import :renderdata;

using namespace draconic::core;
namespace rhi = draconic::rhi;
namespace scene = draconic::scene;
namespace render = draconic::render;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;

export namespace draconic::particles
{
    // Attach a particle effect to an entity. The effect is borrowed (the app owns it now; a cooked
    // ParticleEffectResource later). SetEffect spins up the runtime instance.
    struct ParticleEffectComponent
    {
        ParticleEffect*                      effect = nullptr;   // borrowed
        UniquePtr<ParticleEffectInstance>    instance;
        rhi::TextureView*                    texture = nullptr;  // billboard atlas (borrowed); null = untextured (soft dot)
        // Mesh-mode systems (ParticleRenderMode::Mesh) draw this mesh per particle through the instanced-
        // mesh path. Held while attached; per-particle transform = position * axis/rotation * (size*meshScale).
        RefPtr<geometry::StaticMesh>         mesh;
        RefPtr<materials::Material>          material;
        f32                                  meshScale = 1.0f;
        // Light-mode systems (ParticleRenderMode::Light) add a point light per particle (capped) to the
        // scene's clustered-forward light list, so particles illuminate their surroundings; they also draw
        // the billboard glow. Intensity scales by particle alpha (fades out); range is fixed per emitter.
        f32                                  lightIntensity = 4.0f;
        f32                                  lightRange = 4.0f;
        bool                                 visible = true;

        void SetEffect(ParticleEffect& fx)
        {
            effect = &fx;
            instance = MakeUnique<ParticleEffectInstance>(DefaultAllocator(), fx);
        }
    };

    class ParticleEffectComponentManager final : public scene::ComponentManager<ParticleEffectComponent>,
                                                 public render::IRenderDataProvider
    {
    public:
        void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

        // The dispatch id of the ParticleRenderer (set by the ParticleSubsystem after it registers it).
        void SetBillboardRendererId(u16 id) noexcept { m_billboardRendererId = id; }

        // Scene-driven sim: advance every instance in PostUpdate (before render extraction).
        void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override
        {
            if (phase != scene::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
            ForEach([&](ParticleEffectComponent& c, scene::EntityHandle owner) {
                if (!c.instance) { return; }
                c.instance->position = m_scene->GetWorldPosition(owner);
                c.instance->Update(deltaTime, m_cameraPos);
            });
        }

        // render::IRenderDataProvider: pack each visible billboard system's live particles into a
        // ParticleBillboardRenderData batch and add it to the snapshot. Called during render extraction
        // (this scene's turn). Mesh-mode systems are handled elsewhere (instanced-mesh, later).
        void ExtractRenderData(render::ExtractedScene& snapshot) override
        {
            const u16 billboardRendererId = m_billboardRendererId;
            m_scratchUsed = 0;
            m_xformUsed = 0;
            m_tintUsed = 0;
            ForEach([&](ParticleEffectComponent& c, scene::EntityHandle owner) {
                if (!c.visible || !c.instance) { return; }
                ParticleEffect& fx = c.instance->Effect();
                for (i32 s = 0; s < fx.SystemCount(); ++s)
                {
                    ParticleSystem* sys = fx.GetSystem(s);
                    if (sys == nullptr) { continue; }
                    if (sys->renderMode == ParticleRenderMode::Mesh) { ExtractMeshSystem(*sys, c, owner, s, snapshot); continue; }
                    if (sys->renderMode == ParticleRenderMode::Trail) { continue; }   // ribbons: Phase 5
                    const i32 alive = sys->AliveCount();
                    if (alive <= 0) { continue; }
                    // Light-mode: illuminate the scene with a capped set of point lights (drawn as billboards too).
                    if (sys->renderMode == ParticleRenderMode::Light) { ExtractLights(*sys, c, snapshot); }

                    Array<ParticleBillboardInstance>& scratch = AcquireScratch();
                    scratch.Resize(static_cast<usize>(alive));
                    Vector3 boundsMin{ 1e30f, 1e30f, 1e30f }, boundsMax{ -1e30f, -1e30f, -1e30f };
                    PackBillboards(*sys, scratch.Data(), boundsMin, boundsMax);

                    ParticleBillboardRenderData* rd = snapshot.Add<ParticleBillboardRenderData>();
                    if (rd == nullptr) { continue; }
                    rd->category   = render::RenderCategories::Transparent;
                    rd->rendererId = billboardRendererId;
                    rd->instances  = scratch.Data();
                    rd->count      = static_cast<u32>(alive);
                    rd->texture    = c.texture;
                    rd->blend      = (sys->blendMode == ParticleBlendMode::Additive || sys->blendMode == ParticleBlendMode::Premultiplied) ? 1u : 0u;
                    const Vector3 center = (boundsMin + boundsMax) * 0.5f;
                    rd->worldCenter = center;
                    rd->worldRadius = Length(boundsMax - center) + LargestSize(*sys);
                }
            });
        }

    private:
        // Map a render mode to the shader's orientation mode (0 camera / 1 camera-about-Y / 2 world-XY).
        [[nodiscard]] static f32 OrientationMode(ParticleRenderMode m) noexcept
        {
            switch (m)
            {
                case ParticleRenderMode::VerticalBillboard:   return 1.0f;
                case ParticleRenderMode::HorizontalBillboard: return 2.0f;
                default:                                       return 0.0f;   // Billboard / Stretched
            }
        }

        void PackBillboards(ParticleSystem& sys, ParticleBillboardInstance* out, Vector3& bmin, Vector3& bmax)
        {
            ParticleStreamContainer& st = sys.Streams();
            CPUStream<Vector3>* pos = st.Positions();
            CPUStream<Vector2>* sizes = st.Sizes();
            CPUStream<Vector4>* cols = st.Colors();
            CPUStream<f32>*     rots = st.Rotations();
            CPUStream<Vector3>* vels = st.Velocities();
            const bool stretch = (sys.renderMode == ParticleRenderMode::StretchedBillboard);
            const f32 modeF = OrientationMode(sys.renderMode);
            const f32 softDist = sys.softParticles ? sys.softDistance : 0.0f;   // 0 => no soft fade
            const i32 alive = sys.AliveCount();
            for (i32 i = 0; i < alive; ++i)
            {
                const Vector3 p = (pos != nullptr) ? (*pos)[i] : Vector3::Zero;
                const Vector2 sz = (sizes != nullptr) ? (*sizes)[i] : Vector2{ 0.1f, 0.1f };
                ParticleBillboardInstance& o = out[i];
                o.positionSize = Vector4{ p.x, p.y, p.z, sz.x };
                o.sizeRotMode  = Vector4{ sz.y, (rots != nullptr) ? (*rots)[i] : 0.0f, modeF, softDist };
                o.color        = (cols != nullptr) ? (*cols)[i] : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
                o.uvRect       = Vector4{ 0.0f, 0.0f, 1.0f, 1.0f };
                const Vector3 v = (stretch && vels != nullptr) ? (*vels)[i] : Vector3::Zero;
                o.velocity     = Vector4{ v.x, v.y, v.z, stretch ? 0.1f : 0.0f };
                bmin = Vector3{ Min(bmin.x, p.x), Min(bmin.y, p.y), Min(bmin.z, p.z) };
                bmax = Vector3{ Max(bmax.x, p.x), Max(bmax.y, p.y), Max(bmax.z, p.z) };
            }
        }

        [[nodiscard]] static f32 LargestSize(ParticleSystem& sys) noexcept
        {
            CPUStream<Vector2>* sizes = sys.Streams().Sizes();
            if (sizes == nullptr) { return 0.5f; }
            f32 m = 0.0f;
            for (i32 i = 0; i < sys.AliveCount(); ++i) { m = Max(m, Max((*sizes)[i].x, (*sizes)[i].y)); }
            return m;
        }

        // Per-batch scratch pool (UniquePtr so growth never moves a live buffer; a render-data points
        // into one, valid for the frame). Reused each extraction.
        Array<ParticleBillboardInstance>& AcquireScratch()
        {
            if (m_scratchUsed >= m_scratch.Size()) { m_scratch.PushBack(MakeUnique<Array<ParticleBillboardInstance>>(DefaultAllocator())); }
            return *m_scratch[m_scratchUsed++];
        }

        // Mesh-mode: emit a MultiMeshRenderData drawn by the shared mesh renderer (the instanced-mesh
        // path). Per-particle world transform + tint; a bumped version re-uploads the (dynamic) set each
        // frame. No new renderer needed - particles reuse the InstancedMesh persistent-buffer machinery.
        void ExtractMeshSystem(ParticleSystem& sys, ParticleEffectComponent& c, scene::EntityHandle owner, i32 sysIndex, render::ExtractedScene& snapshot)
        {
            const i32 alive = sys.AliveCount();
            if (alive <= 0 || c.mesh.Get() == nullptr) { return; }

            Array<Matrix4>& xf   = AcquireXformScratch();
            Array<Color>&   tint = AcquireTintScratch();
            xf.Resize(static_cast<usize>(alive));
            tint.Resize(static_cast<usize>(alive));
            Vector3 bmin{ 1e30f, 1e30f, 1e30f }, bmax{ -1e30f, -1e30f, -1e30f };
            PackMeshTransforms(sys, c.meshScale, xf.Data(), tint.Data(), bmin, bmax);

            render::MultiMeshRenderData* rd = snapshot.Add<render::MultiMeshRenderData>();
            if (rd == nullptr) { return; }
            rd->multiMesh     = true;
            rd->key           = (static_cast<u64>(owner.index) << 16) | static_cast<u64>(static_cast<u32>(sysIndex) & 0xFFFFu);
            rd->transforms    = xf.Data();
            rd->tints         = tint.Data();
            rd->instanceCount = static_cast<u32>(alive);
            rd->version       = ++m_meshVersion;   // dynamic: transforms change every frame -> re-upload
            rd->mesh          = c.mesh.Get();
            rd->material      = c.material.Get();
            rd->rendererId    = 0;   // the mesh renderer (id 0)
            rd->category      = render::RenderCategories::Opaque;   // solid mesh particles (debris); transparent later
            const Vector3 center = (bmin + bmax) * 0.5f;
            rd->worldCenter   = center;
            rd->worldRadius   = Length(bmax - center) + LargestSize(sys) * c.meshScale;
        }

        // Light-mode: add a point light per particle (capped + evenly strided across the set to stay
        // under the forward light budget), colored by the particle, intensity faded by its alpha.
        void ExtractLights(ParticleSystem& sys, ParticleEffectComponent& c, render::ExtractedScene& snapshot)
        {
            CPUStream<Vector3>* pos = sys.Streams().Positions();
            if (pos == nullptr) { return; }
            CPUStream<Vector4>* cols = sys.Streams().Colors();
            const i32 alive = sys.AliveCount();
            const i32 cap = Min(alive, kLightParticleCap);
            const i32 step = Max(1, alive / Max(cap, 1));
            i32 added = 0;
            for (i32 i = 0; added < cap && i < alive; i += step, ++added)
            {
                render::GpuLight g;
                g.positionWS  = (*pos)[i];
                g.range       = c.lightRange;
                const Vector4 cv = (cols != nullptr) ? (*cols)[i] : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
                g.color       = Vector3{ cv.x, cv.y, cv.z };
                g.intensity   = c.lightIntensity * cv.w;   // fade with the particle's alpha
                g.directionWS = Vector3{ 0.0f, -1.0f, 0.0f };
                g.type        = 1.0f;    // point (LightType::Point)
                g.shadowIndex = -1.0f;   // particles don't cast shadows
                snapshot.AddLight(g);
            }
        }

        void PackMeshTransforms(ParticleSystem& sys, f32 meshScale, Matrix4* xf, Color* tint, Vector3& bmin, Vector3& bmax)
        {
            ParticleStreamContainer& st = sys.Streams();
            CPUStream<Vector3>* pos = st.Positions();
            CPUStream<Vector2>* sizes = st.Sizes();
            CPUStream<Vector4>* cols = st.Colors();
            CPUStream<Vector3>* axes = st.Axes();
            CPUStream<f32>*     rots = st.Rotations();
            const i32 alive = sys.AliveCount();
            for (i32 i = 0; i < alive; ++i)
            {
                const Vector3 p = (pos != nullptr) ? (*pos)[i] : Vector3::Zero;
                const f32 sz = ((sizes != nullptr) ? (*sizes)[i].x : 0.1f) * meshScale;
                Transform t;
                t.position = p;
                t.scale    = Vector3{ sz, sz, sz };
                if (rots != nullptr) {
                    const Vector3 axis = (axes != nullptr) ? (*axes)[i] : Vector3::UnitY;
                    t.rotation = Quaternion::FromAxisAngle((LengthSquared(axis) > 1e-6f) ? Normalized(axis) : Vector3::UnitY, (*rots)[i]);
                }
                xf[i] = t.ToMatrix();
                const Vector4 cv = (cols != nullptr) ? (*cols)[i] : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
                tint[i] = Color{ cv.x, cv.y, cv.z, cv.w };
                bmin = Vector3{ Min(bmin.x, p.x), Min(bmin.y, p.y), Min(bmin.z, p.z) };
                bmax = Vector3{ Max(bmax.x, p.x), Max(bmax.y, p.y), Max(bmax.z, p.z) };
            }
        }

        Array<Matrix4>& AcquireXformScratch()
        {
            if (m_xformUsed >= m_xformScratch.Size()) { m_xformScratch.PushBack(MakeUnique<Array<Matrix4>>(DefaultAllocator())); }
            return *m_xformScratch[m_xformUsed++];
        }
        Array<Color>& AcquireTintScratch()
        {
            if (m_tintUsed >= m_tintScratch.Size()) { m_tintScratch.PushBack(MakeUnique<Array<Color>>(DefaultAllocator())); }
            return *m_tintScratch[m_tintUsed++];
        }

        static constexpr i32 kLightParticleCap = 48;   // max point lights one Light system contributes/frame

        scene::Scene* m_scene = nullptr;
        Vector3       m_cameraPos{ 0.0f, 0.0f, 0.0f };
        u16           m_billboardRendererId = 0;
        Array<UniquePtr<Array<ParticleBillboardInstance>>> m_scratch;
        usize         m_scratchUsed = 0;
        Array<UniquePtr<Array<Matrix4>>> m_xformScratch;   // mesh-particle world transforms
        Array<UniquePtr<Array<Color>>>   m_tintScratch;    // mesh-particle per-instance tints
        usize         m_xformUsed = 0;
        usize         m_tintUsed = 0;
        u32           m_meshVersion = 0;   // bumped per mesh batch so the instanced-mesh path re-uploads
    };
}
