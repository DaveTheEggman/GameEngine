// draconic.particles.subsystem:renderdata - the render-data the particle billboard path produces.
//
// A ParticleBillboardRenderData is a BATCH: one item per (system, texture, blend) carrying a
// borrowed pointer to `count` packed billboard instances. This is the key difference from the
// sprite path (one item per sprite) - particle counts would flood the draw-list sort otherwise.
// The ParticleRenderer packs the batch into its instance ring and emits one instanced draw.

module;
#include "Core/Prelude.h"
#include <type_traits>

export module draconic.particles.subsystem:renderdata;

import draconic.core;
import draconic.rhi;
import draconic.render;   // RenderData base + RenderCategories

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::particles
{
    // One packed billboard instance (80 bytes = 5x float4, matches the ParticleRenderer VS inputs).
    struct ParticleBillboardInstance
    {
        Vector4 positionSize;   // xyz world center, w width
        Vector4 sizeRotMode;    // x height, y rotation (radians), z orientation mode, w unused
        Vector4 color;          // rgba (linear, premultiply/scale done in sim)
        Vector4 uvRect;         // xy uv min, zw uv size (flipbook)
        Vector4 velocity;       // xyz world velocity, w stretch scale (0 = plain billboard)
    };
    static_assert(sizeof(ParticleBillboardInstance) == 80);

    // Batched billboard draw for one system (or texture/blend group). `instances` is borrowed and
    // valid for the frame (owned by the component manager's scratch). Rides the Transparent category.
    struct ParticleBillboardRenderData : draconic::render::RenderData
    {
        const ParticleBillboardInstance* instances = nullptr;
        u32                              count = 0;
        rhi::TextureView*                texture = nullptr;
        u8                               blend = 0;   // 0 = alpha, 1 = additive (mapped from ParticleBlendMode)
    };
    static_assert(std::is_trivially_destructible_v<ParticleBillboardRenderData>);
}
