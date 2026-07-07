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
import draconic.render;             // ExtractedScene, RenderCategories
import draconic.particles;          // ParticleEffect / ParticleEffectInstance / ParticleSystem
import :renderdata;

using namespace draconic::core;
namespace rhi = draconic::rhi;
namespace scene = draconic::scene;
namespace render = draconic::render;

export namespace draconic::particles
{
    // Attach a particle effect to an entity. The effect is borrowed (the app owns it now; a cooked
    // ParticleEffectResource later). SetEffect spins up the runtime instance.
    struct ParticleEffectComponent
    {
        ParticleEffect*                      effect = nullptr;   // borrowed
        UniquePtr<ParticleEffectInstance>    instance;
        rhi::TextureView*                    texture = nullptr;  // billboard atlas (borrowed); null = untextured (white)
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
            ForEach([&](ParticleEffectComponent& c, scene::EntityHandle) {
                if (!c.visible || !c.instance) { return; }
                ParticleEffect& fx = c.instance->Effect();
                for (i32 s = 0; s < fx.SystemCount(); ++s)
                {
                    ParticleSystem* sys = fx.GetSystem(s);
                    if (sys == nullptr || sys->renderMode == ParticleRenderMode::Mesh) { continue; }
                    const i32 alive = sys->AliveCount();
                    if (alive <= 0) { continue; }

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
            const i32 alive = sys.AliveCount();
            for (i32 i = 0; i < alive; ++i)
            {
                const Vector3 p = (pos != nullptr) ? (*pos)[i] : Vector3::Zero;
                const Vector2 sz = (sizes != nullptr) ? (*sizes)[i] : Vector2{ 0.1f, 0.1f };
                ParticleBillboardInstance& o = out[i];
                o.positionSize = Vector4{ p.x, p.y, p.z, sz.x };
                o.sizeRotMode  = Vector4{ sz.y, (rots != nullptr) ? (*rots)[i] : 0.0f, modeF, 0.0f };
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

        scene::Scene* m_scene = nullptr;
        Vector3       m_cameraPos{ 0.0f, 0.0f, 0.0f };
        u16           m_billboardRendererId = 0;
        Array<UniquePtr<Array<ParticleBillboardInstance>>> m_scratch;
        usize         m_scratchUsed = 0;
    };
}
