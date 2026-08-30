// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ThumbnailStage implementation (see ThumbnailStage.cppm). The heavy render/scene/rhi imports
// live here behind Impl, exactly like PreviewViewportImpl.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.preview;

import foundation.core;
import foundation.runtime;
import foundation.runtime.client;
import foundation.graphics;
import foundation.rhi;
import foundation.image;
import foundation.scene;
import engine.scene;
import foundation.render;
import engine.render;
import foundation.resource;
import editor.core;

using namespace foundation::core;

namespace editor
{
    namespace rhi = foundation::rhi;
    namespace render = foundation::render;
    namespace image = foundation::image;
    namespace scene = foundation::scene;

    namespace
    {
        constexpr u32 kTileSize = ThumbnailService::kThumbnailSize;
        constexpr u32 kRenderSize = kTileSize * ThumbnailStage::kSupersample;

        // IEEE half -> float (the render target is RGBA16Float, matching every viewport view
        // so no pass rebuilds pipelines per format).
        [[nodiscard]] f32 HalfToFloat(u16 h)
        {
            const u32 sign = static_cast<u32>(h >> 15) & 1u;
            const u32 exponent = static_cast<u32>(h >> 10) & 0x1Fu;
            const u32 mantissa = static_cast<u32>(h) & 0x3FFu;
            u32 bits;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign << 31; // signed zero
                }
                else
                {
                    // Subnormal half: normalize into a float exponent.
                    u32 e = 127 - 15 + 1;
                    u32 m = mantissa;
                    while ((m & 0x400u) == 0)
                    {
                        m <<= 1;
                        --e;
                    }
                    bits = (sign << 31) | (e << 23) | ((m & 0x3FFu) << 13);
                }
            }
            else if (exponent == 0x1F)
            {
                bits = (sign << 31) | 0x7F800000u | (mantissa << 13); // inf / nan
            }
            else
            {
                bits = (sign << 31) | ((exponent - 15 + 127) << 23) | (mantissa << 13);
            }
            f32 value;
            static_assert(sizeof(value) == sizeof(bits));
            __builtin_memcpy(&value, &bits, sizeof(value));
            return value;
        }

        // Exact integer box downscale (kSupersample x kSupersample average per output texel)
        // over RGBA16Float source rows. The tonemap pass already applied the sRGB OETF, so the
        // values quantize to bytes directly - encoding again would double-gamma the image.
        void Downscale(const u8* src, u32 srcRowBytes, image::Image& out)
        {
            constexpr u32 kFactor = ThumbnailStage::kSupersample;
            out = image::Image(kTileSize, kTileSize, image::PixelFormat::RGBA8);
            u8* dst = out.PixelDataMut().Data();
            for (u32 y = 0; y < kTileSize; ++y)
            {
                for (u32 x = 0; x < kTileSize; ++x)
                {
                    f32 sum[4] = {0, 0, 0, 0};
                    for (u32 sy = 0; sy < kFactor; ++sy)
                    {
                        const u8* row = src + static_cast<usize>(y * kFactor + sy) * srcRowBytes +
                                        static_cast<usize>(x) * kFactor * 8u;
                        for (u32 sx = 0; sx < kFactor; ++sx)
                        {
                            const u16* texel = reinterpret_cast<const u16*>(row + sx * 8u);
                            sum[0] += HalfToFloat(texel[0]);
                            sum[1] += HalfToFloat(texel[1]);
                            sum[2] += HalfToFloat(texel[2]);
                            sum[3] += HalfToFloat(texel[3]);
                        }
                    }
                    constexpr f32 kInvSamples = 1.0f / (kFactor * kFactor);
                    u8* texel = dst + (static_cast<usize>(y) * kTileSize + x) * 4u;
                    for (u32 c = 0; c < 4; ++c)
                    {
                        texel[c] = static_cast<u8>(
                            Clamp(sum[c] * kInvSamples, 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                }
            }
        }
    }

    struct ThumbnailStage::Impl
    {
        enum class State : u8
        {
            Idle,          // no job; Update polls the service
            Staging,       // generator populating the scene (Pending until resources resolve)
            RenderPending, // staged + framed; the next Render draws + encodes the readback
            AwaitReadback, // copy submitted on frame `submittedIndex`; retire on ring re-visit
        };

        runtime::IApplicationHost* host = nullptr;
        ThumbnailService* service = nullptr;
        foundation::resource::ResourceManager* resources = nullptr;
        engine::scene::SceneSubsystem* scenes = nullptr;
        engine::render::RenderSubsystem* render = nullptr;
        scene::SceneManager sceneManager; // the stage's OWN scene group (never simulated)
        scene::Scene* scene = nullptr;

        // GPU objects (created lazily on the first Render with a live device).
        rhi::Device* device = nullptr;
        rhi::Texture* target = nullptr;
        rhi::TextureView* targetView = nullptr;
        rhi::Buffer* readback = nullptr;
        rhi::ResourceState targetState = rhi::ResourceState::Undefined;

        State state = State::Idle;
        SceneThumbnailJob job{};
        f32 radius = 1.0f;
        u32 stagingFrames = 0;
        u32 submittedIndex = 0;
        bool sawOtherIndex = false; // the ring must LEAVE the submitted slot before it retires
        bool warnedDisabled = false; // one-shot wiring-bug warning (see Update)

        [[nodiscard]] bool EnsureGpuObjects()
        {
            if (target != nullptr)
            {
                return true;
            }
            if (host->Graphics() == nullptr)
            {
                return false;
            }
            device = host->Graphics()->Raw();
            if (device == nullptr)
            {
                return false;
            }
            rhi::TextureDesc desc;
            desc.width = kRenderSize;
            desc.height = kRenderSize;
            desc.format = rhi::TextureFormat::RGBA16Float;
            desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
            if (!device->CreateTexture(desc, target).IsOk() || target == nullptr)
            {
                target = nullptr;
                return false;
            }
            if (!device->CreateTextureView(target, rhi::TextureViewDesc{}, targetView).IsOk() ||
                targetView == nullptr)
            {
                device->DestroyTexture(target);
                target = nullptr;
                return false;
            }
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = static_cast<u64>(kRenderSize) * kRenderSize * 8u;
            bufferDesc.usage = rhi::BufferUsage::CopyDst;
            bufferDesc.memory = rhi::MemoryLocation::GpuToCpu;
            if (!device->CreateBuffer(bufferDesc, readback).IsOk() || readback == nullptr)
            {
                device->DestroyTextureView(targetView);
                device->DestroyTexture(target);
                targetView = nullptr;
                target = nullptr;
                readback = nullptr;
                return false;
            }
            targetState = rhi::ResourceState::Undefined;
            return true;
        }

        void FailJob()
        {
            if (job.generator != nullptr && scene != nullptr)
            {
                job.generator->Unstage(*scene);
            }
            service->AcceptSceneResult(job.id, image::Image{}, false);
            job = {};
            state = State::Idle;
        }
    };

    ThumbnailStage::ThumbnailStage(runtime::IApplicationHost& host, ThumbnailService& service,
                                   foundation::resource::ResourceManager* resources)
        : m_impl(MakeUnique<Impl>(DefaultAllocator()))
    {
        m_impl->host = &host;
        m_impl->service = &service;
        m_impl->resources = resources;
        m_impl->scenes = host.Ctx().GetSubsystem<engine::scene::SceneSubsystem>();
        m_impl->render = host.Ctx().GetSubsystem<engine::render::RenderSubsystem>();
        if (m_impl->scenes != nullptr)
        {
            m_impl->scenes->RegisterManager(&m_impl->sceneManager);
            m_impl->scene = m_impl->sceneManager.CreateScene(u8"thumbnails.stage");
            m_impl->scene->SetSimulationEnabled(false);
        }
    }

    ThumbnailStage::~ThumbnailStage()
    {
        Shutdown();
    }

    bool ThumbnailStage::IsBusy() const
    {
        return m_impl->state != Impl::State::Idle;
    }

    void ThumbnailStage::Update()
    {
        Impl& impl = *m_impl;
        if (impl.scene == nullptr || impl.render == nullptr || impl.resources == nullptr)
        {
            // A disabled stage with queued work is a wiring bug (a null resource manager at
            // construction stalled every job, silently, once) - say so exactly once.
            if (!impl.warnedDisabled && impl.service->QueuedSceneJobs() > 0)
            {
                impl.warnedDisabled = true;
                LOG_WARNING(u8"Thumbnails",
                            u8"stage disabled (scene {} / renderer {} / resources {}) with {} "
                            u8"queued jobs - GPU thumbnails will not generate",
                            impl.scene != nullptr, impl.render != nullptr,
                            impl.resources != nullptr, impl.service->QueuedSceneJobs());
            }
            return;
        }

        if (impl.state == Impl::State::Idle)
        {
            SceneThumbnailJob job = impl.service->TakeSceneJob();
            if (job.id.IsNil() || job.generator == nullptr)
            {
                return;
            }
            impl.job = job;
            impl.stagingFrames = 0;
            impl.state = Impl::State::Staging;
        }

        if (impl.state == Impl::State::Staging)
        {
            f32 radius = 1.0f;
            const ThumbnailStageStep step =
                impl.job.generator->Stage(impl.job.id, *impl.scene, *impl.resources, radius);
            switch (step)
            {
            case ThumbnailStageStep::Ready:
                impl.radius = Max(radius, 0.001f);
                impl.state = Impl::State::RenderPending;
                break;
            case ThumbnailStageStep::Failed:
                impl.FailJob();
                break;
            case ThumbnailStageStep::Pending:
                if (++impl.stagingFrames > kMaxStagingFrames)
                {
                    LOG_WARNING(u8"Thumbnails", u8"stage timed out waiting on resources for {}",
                                impl.job.id);
                    impl.FailJob();
                }
                break;
            }
        }
    }

    void ThumbnailStage::Render(foundation::graphics::FrameContext& frame)
    {
        Impl& impl = *m_impl;
        if (!frame.valid || frame.encoder == nullptr || impl.render == nullptr ||
            !impl.render->IsReady() || impl.scene == nullptr)
        {
            return;
        }

        if (impl.state == Impl::State::AwaitReadback)
        {
            // Retire when the device ring has LEFT and RETURNED to the submission's slot: at
            // that point the submission (and its copy) has provably completed, so the map
            // cannot stall the frame.
            if (frame.frameIndex != impl.submittedIndex)
            {
                impl.sawOtherIndex = true;
                return;
            }
            if (!impl.sawOtherIndex)
            {
                return;
            }
            image::Image pixels;
            if (const u8* mapped = static_cast<const u8*>(impl.readback->Map()))
            {
                Downscale(mapped, kRenderSize * 8u, pixels);
                impl.readback->Unmap();
                if (impl.job.generator != nullptr)
                {
                    impl.job.generator->Unstage(*impl.scene);
                }
                impl.service->AcceptSceneResult(impl.job.id, Move(pixels), true);
                impl.job = {};
                impl.state = Impl::State::Idle;
            }
            else
            {
                impl.FailJob();
            }
            return;
        }

        if (impl.state != Impl::State::RenderPending)
        {
            return;
        }
        if (!impl.EnsureGpuObjects())
        {
            return; // no device yet; retry next frame
        }

        // The stage scene never ticks a simulation, but its transforms must be current for
        // extraction (generators set entity transforms during Stage).
        impl.scene->UpdateTransforms();

        // Ortho along (1,1,1): rotation-invariant framing sized from the staged bounds.
        const Float3 direction = Normalized(Float3{1.0f, 1.0f, 1.0f});
        const f32 distance = impl.radius * 4.0f;
        const f32 halfExtent = impl.radius * 1.1f;
        render::ViewCamera camera;
        const Float3 eye = direction * distance;
        camera.view = Float4x4::LookAtRH(eye, Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 1.0f, 0.0f});
        camera.projection = Float4x4::OrthographicRH(halfExtent * 2.0f, halfExtent * 2.0f, 0.05f,
                                                     distance + impl.radius * 4.0f);
        camera.position = eye;
        camera.farZ = distance + impl.radius * 4.0f;

        render::CameraOverride cameraOverride;
        cameraOverride.camera = camera;
        cameraOverride.clearColor = Color{0.10f, 0.11f, 0.13f, 1.0f};

        render::TargetState targetState;
        targetState.texture = impl.target;
        targetState.currentState = impl.targetState;
        targetState.finalState = rhi::ResourceState::CopySrc;

        impl.render->RenderScene(*impl.scene, impl.targetView, rhi::TextureFormat::RGBA16Float,
                                 kRenderSize, kRenderSize,
                                 render::ViewportRect{0, 0, kRenderSize, kRenderSize},
                                 &cameraOverride, targetState);
        impl.targetState = rhi::ResourceState::CopySrc;

        // The copy rides the FRAME encoder, so it is ordered after the scene's passes.
        rhi::BufferTextureCopyRegion region;
        region.bytesPerRow = kRenderSize * 8u;
        region.rowsPerImage = kRenderSize;
        region.textureExtent = rhi::Extent3D{kRenderSize, kRenderSize, 1};
        frame.encoder->CopyTextureToBuffer(impl.target, impl.readback, region);

        impl.submittedIndex = frame.frameIndex;
        impl.sawOtherIndex = false;
        impl.state = Impl::State::AwaitReadback;
    }

    void ThumbnailStage::Shutdown()
    {
        Impl& impl = *m_impl;
        if (impl.state != Impl::State::Idle && impl.job.generator != nullptr &&
            impl.scene != nullptr)
        {
            impl.job.generator->Unstage(*impl.scene);
        }
        impl.job = {};
        impl.state = Impl::State::Idle;

        if (impl.device != nullptr)
        {
            impl.device->WaitIdle(); // an in-flight readback copy may still reference these
            if (impl.readback != nullptr)
            {
                impl.device->DestroyBuffer(impl.readback);
                impl.readback = nullptr;
            }
            if (impl.targetView != nullptr)
            {
                impl.device->DestroyTextureView(impl.targetView);
                impl.targetView = nullptr;
            }
            if (impl.target != nullptr)
            {
                impl.device->DestroyTexture(impl.target);
                impl.target = nullptr;
            }
            impl.device = nullptr;
        }

        if (impl.scene != nullptr && impl.scenes != nullptr)
        {
            impl.sceneManager.Clear();
            impl.scenes->UnregisterManager(&impl.sceneManager);
            impl.scene = nullptr;
        }
    }
}
