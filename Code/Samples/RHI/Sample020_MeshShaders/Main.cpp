#include <new>
/// Sample020 -- Mesh Shaders (Rotating Triangle). Ported from Sedulous Sample020_MeshShaders.
/// Demonstrates mesh shader pipeline: a rotating triangle generated entirely in the mesh shader.

#include <cstdio>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

struct PushData {
    float time;
    float aspectRatio;
    float pad0;
    float pad1;
};

class MeshShaderSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample020 - Mesh Shaders (Rotating Triangle)"; }
protected:
    dr::DeviceFeatures requiredFeatures() const override {
        dr::DeviceFeatures f{};
        f.meshShaders = true;
        return f;
    }
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    static constexpr const char8_t kMeshShaderSource[] = u8R"(
        struct PushConstants
        {
            float Time;
            float AspectRatio;
            float Pad0, Pad1;
        };

        [[vk::push_constant]] ConstantBuffer<PushConstants> pc : register(b0, space0);

        struct MeshOutput
        {
            float4 Position : SV_POSITION;
            float3 Color    : TEXCOORD0;
        };

        [outputtopology("triangle")]
        [numthreads(1, 1, 1)]
        void MSMain(out vertices MeshOutput verts[3], out indices uint3 tris[1])
        {
            SetMeshOutputCounts(3, 1);

            float angle = pc.Time * 0.5;
            float c = cos(angle);
            float s = sin(angle);

            float2 positions[3] = {
                float2( 0.0,  0.5),
                float2(-0.5, -0.5),
                float2( 0.5, -0.5)
            };

            float3 colors[3] = {
                float3(1.0, 0.0, 0.0),
                float3(0.0, 1.0, 0.0),
                float3(0.0, 0.0, 1.0)
            };

            for (uint i = 0; i < 3; i++)
            {
                float2 p = positions[i];
                float2 rotated = float2(p.x * c - p.y * s, p.x * s + p.y * c);
                rotated.x /= pc.AspectRatio;

                verts[i].Position = float4(rotated, 0.0, 1.0);
                verts[i].Color = colors[i];
            }

            tris[0] = uint3(0, 1, 2);
        }
    )";

    static constexpr const char8_t kFragmentShaderSource[] = u8R"(
        struct PSInput
        {
            float4 Position : SV_POSITION;
            float3 Color    : TEXCOORD0;
        };

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return float4(input.Color, 1.0);
        }
    )";

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule* meshModule_ = nullptr;
    dr::ShaderModule* fragModule_ = nullptr;
    dr::PipelineLayout* pipelineLayout_ = nullptr;
    dr::MeshPipeline* meshPipeline_ = nullptr;
    dr::CommandPool* pool_ = nullptr;
    dr::Fence* fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status MeshShaderSample::onInit() {
    using raptor::core::Status, raptor::core::Span;

    // Check mesh shader support.
    if (!device_->features.meshShaders) {
        std::fprintf(stderr, "ERROR: Mesh shaders are not supported by this device/backend\n");
        return raptor::core::ErrorCode::Unknown;
    }

    // Shader compiler.
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compile mesh shader (SM 6.5 required for mesh shaders).
    if (sf::compileToModule(compiler_, device_, kMeshShaderSource, ds::ShaderStage::Mesh,
                            u"MSMain", u"MeshShader", u"6_5", meshModule_) != raptor::core::ErrorCode::Ok)
        return raptor::core::ErrorCode::Unknown;

    // Compile fragment shader.
    if (sf::compileToModule(compiler_, device_, kFragmentShaderSource, ds::ShaderStage::Fragment,
                            u"PSMain", u"FragmentShader", fragModule_) != raptor::core::ErrorCode::Ok)
        return raptor::core::ErrorCode::Unknown;

    // Pipeline layout with push constants.
    dr::PushConstantRange pushRange{};
    pushRange.stages = dr::ShaderStage::Mesh;
    pushRange.offset = 0;
    pushRange.size   = sizeof(PushData);

    dr::PipelineLayoutDesc pld{};
    pld.pushConstantRanges = Span<const dr::PushConstantRange>(&pushRange, 1);
    pld.label = u"MeshPipelineLayout";
    if (device_->CreatePipelineLayout(pld, pipelineLayout_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create mesh pipeline.
    dr::ColorTargetState ct{};
    ct.format   = swapChain_->Format();
    ct.writeMask = dr::ColorWriteMask::All;

    dr::MeshPipelineDesc mpd{};
    mpd.layout       = pipelineLayout_;
    mpd.mesh         = { meshModule_, u"MSMain", dr::ShaderStage::Mesh };
    mpd.fragment     = dr::FragmentState{};
    mpd.fragment->shader  = { fragModule_, u"PSMain", dr::ShaderStage::Fragment };
    mpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    mpd.colorTargets = Span<const dr::ColorTargetState>(&ct, 1);
    mpd.label        = u"MeshShaderPipeline";
    if (device_->CreateMeshPipeline(mpd, meshPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Command pool and fence.
    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

void MeshShaderSample::onRender() {
    using raptor::core::f32, raptor::core::Span;

    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Barrier: present -> render target.
    enc->TransitionTexture(swapChain_->CurrentTexture(),
                           dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Begin render pass.
    dr::ColorAttachment ca{};
    ca.view       = swapChain_->CurrentTextureView();
    ca.loadOp     = dr::LoadOp::Clear;
    ca.storeOp    = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
    rp->SetScissor(0, 0, width_, height_);

    // Draw with mesh shader.
    if (auto* meshPass = rp->AsMeshShaderExt()) {
        meshPass->SetMeshPipeline(meshPipeline_);

        // Push constants (must be after pipeline is bound).
        PushData pushData{};
        pushData.time        = totalTime_;
        pushData.aspectRatio = static_cast<float>(width_) / static_cast<float>(height_);
        pushData.pad0        = 0.0f;
        pushData.pad1        = 0.0f;
        rp->SetPushConstants(dr::ShaderStage::Mesh, 0, sizeof(PushData), &pushData);

        meshPass->DrawMeshTasks(1);
    }

    rp->End();

    // Barrier: render target -> present.
    enc->TransitionTexture(swapChain_->CurrentTexture(),
                           dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish();
    fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void MeshShaderSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (meshPipeline_) device_->DestroyMeshPipeline(meshPipeline_);
    if (pipelineLayout_) device_->DestroyPipelineLayout(pipelineLayout_);
    if (fragModule_) device_->DestroyShaderModule(fragModule_);
    if (meshModule_) device_->DestroyShaderModule(meshModule_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MeshShaderSample app; return app.run(argc, argv); }
