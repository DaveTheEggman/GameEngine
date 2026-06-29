// Raptor::RuntimeGraphics — the `raptor.runtime.graphics` module.
//
// The RHI render host, promoted out of the per-sample bring-up code so samples,
// the UI, and the renderer share one tested path. Two pieces:
//
//   GraphicsDevice — the SHARED GPU: backend (validation-wrapped) + adapter +
//     logical device + graphics queue, plus the CPU frame-in-flight ring index.
//     Created once for the whole app. Hands out RenderWindows.
//
//   RenderWindow — a single window's PRESENTATION target: surface + swapchain +
//     a per-window ring of command pools/fences. Created/destroyed at runtime
//     (the basis for detachable UI windows). The main window is just the first.
//
// A FrameContext is the per-window, per-frame hand-off the host gives a consumer:
// an acquired backbuffer + a host-created encoder. The host owns acquire / fence
// sync / submit / present and the backbuffer state transitions; the consumer
// records content (or calls BeginBackbufferPass for the common clear+pass case).
//
// Multi-window is uniform: there is no "main window" special case here — an app
// renders a list of RenderWindows, each independent, sharing one GraphicsDevice.

module;
#include "Core/Prelude.h"

export module raptor.runtime.graphics;

import raptor.core;
import raptor.rhi;
import raptor.runtime.platform;
// Backend factories live in sibling modules so this core host imports only the
// base RHI (keeps it GPU-backend-agnostic and avoids importing heavy backend
// modules into this interface — which GCC's module reader chokes on):
//   raptor.runtime.graphics.null — CreateNullGraphicsDevice (headless)
//   raptor.runtime.graphics.gpu  — CreateGraphicsDevice (Vulkan/DX12)

namespace rc = raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::runtime
{
    // Null is a real headless option (CI / servers / tests) — no GPU required.
    enum class BackendType : rc::u8 { Vulkan, DX12, Null };

    struct GraphicsDeviceDesc
    {
        BackendType        backend          = BackendType::Vulkan;
        // Validation (our RHI-layer wrapper AND the backend's own layers, e.g. Vulkan validation)
        // defaults ON for dev builds and OFF for optimized/shipping builds (RAPTOR_RELEASE — set for
        // Release/RelWithDebInfo/MinSizeRel). A profiling run (RelWithDebInfo) therefore measures the
        // real cost, not the validation overhead. Override explicitly to force either way.
        // NB: RAPTOR_RELEASE is ALWAYS defined (0 in dev, 1 in optimized builds — see Core/Prelude.h),
        // so this must be `#if`, not `#ifdef` (which would always take the release branch).
#if RAPTOR_RELEASE
        bool               enableValidation = false;
#else
        bool               enableValidation = true;
#endif
        rc::u32            framesInFlight    = 2;     // CPU-ahead ring depth
        rhi::DeviceFeatures requiredFeatures = {};
    };

    struct RenderWindowDesc
    {
        rhi::TextureFormat format      = rhi::TextureFormat::BGRA8UnormSrgb;
        rhi::PresentMode   presentMode = rhi::PresentMode::Fifo;
        rc::u32            bufferCount = 2;            // swapchain images
    };

    class GraphicsDevice;   // forward (RenderWindow refs it; defined complete below)
    class RenderWindow;     // forward (FrameContext refs it)

    // Typed per-window payload. The UI layer stashes its {RootView, VGContext,
    // VGRenderer} here without the host knowing the type — capability As*() idiom.
    class IRenderWindowData
    {
    public:
        virtual ~IRenderWindowData() = default;
    };

    // Per-window, per-frame hand-off. Returned by RenderWindow::BeginFrame and
    // passed to consumers (Subsystem::Render / IApplicationModule::OnRenderWindow).
    struct FrameContext
    {
        bool                 valid          = false;   // false => skip (minimized/acquire failed)
        RenderWindow*        window         = nullptr; // which window this frame targets
        rc::u32              frameIndex     = 0;       // device ring index (0..framesInFlight-1)
        rc::u32              width          = 0;
        rc::u32              height         = 0;
        rhi::CommandEncoder* encoder        = nullptr; // primary, host-created
        rhi::CommandPool*    pool           = nullptr; // this frame's pool (extra encoders)
        rhi::Texture*        backbuffer     = nullptr;
        rhi::TextureView*    backbufferView = nullptr;

        // Convenience for the common 2D/UI case: open a render pass that clears
        // and targets the backbuffer (the Undefined->RenderTarget transition was
        // already done by the host in BeginFrame). A RenderGraph-driven renderer
        // ignores this and records on `encoder` directly.
        rhi::RenderPassEncoder* BeginBackbufferPass(rhi::ClearColor clear)
        {
            rhi::ColorAttachment ca{};
            ca.view       = backbufferView;
            ca.loadOp     = rhi::LoadOp::Clear;
            ca.storeOp    = rhi::StoreOp::Store;
            ca.clearValue = clear;
            rhi::RenderPassDesc rpd{};
            rpd.colorAttachments.Add(ca);
            m_pass = (encoder != nullptr) ? encoder->BeginRenderPass(rpd) : nullptr;
            return m_pass;
        }
        void EndBackbufferPass() { if (m_pass != nullptr) { m_pass->End(); m_pass = nullptr; } }

        // One-call clear of the backbuffer (open a clear pass, close it). For
        // minimal apps that just want a visible, cleared window without touching
        // RHI types.
        void Clear(rc::f32 r, rc::f32 g, rc::f32 b, rc::f32 a = 1.0f)
        {
            rhi::ClearColor c; c.r = r; c.g = g; c.b = b; c.a = a;
            BeginBackbufferPass(c);
            EndBackbufferPass();
        }

    private:
        rhi::RenderPassEncoder* m_pass = nullptr;
    };

    // A window's presentation target. Owns its surface/swapchain and a per-frame
    // ring of command pools + fences (per-window present sync). Destroyed via the
    // GraphicsDevice that made it.
    class RenderWindow
    {
    public:
        // Built by GraphicsDevice::CreateRenderWindow; takes ownership of the RHI
        // objects. Arrays are sized to framesInFlight.
        RenderWindow(GraphicsDevice& device, IWindow& window,
                     rhi::Surface* surface, rhi::SwapChain* swapChain,
                     rc::Array<rhi::CommandPool*>&& pools, rc::Array<rhi::Fence*>&& fences) noexcept
            : m_device(&device), m_window(&window), m_surface(surface), m_swapChain(swapChain),
              m_pools(static_cast<rc::Array<rhi::CommandPool*>&&>(pools)),
              m_fences(static_cast<rc::Array<rhi::Fence*>&&>(fences)),
              m_width(window.Width()), m_height(window.Height())
        {
            m_fenceValues.Resize(m_fences.Size(), 0ull);
        }

        ~RenderWindow();

        RenderWindow(const RenderWindow&) = delete;
        RenderWindow& operator=(const RenderWindow&) = delete;

        [[nodiscard]] IWindow& Window() noexcept { return *m_window; }
        [[nodiscard]] rhi::SwapChain* Swap() noexcept { return m_swapChain; }

        // Poll the window size; recreate the swapchain if it changed. Returns true
        // when a resize happened.
        bool SyncSize();

        // Acquire this window's backbuffer and open a host-created encoder from the
        // current frame's pool (after the per-window fence guards reuse). The
        // returned FrameContext is invalid when the window is minimized/zero-sized
        // or acquisition fails — the caller skips rendering it this frame.
        FrameContext BeginFrame();

        // Transition the backbuffer to Present, submit (signalling the per-window
        // fence), and present. No-op for an invalid frame.
        void EndFrame(FrameContext& frame);

        [[nodiscard]] IRenderWindowData* Data() noexcept { return m_data.Get(); }
        void SetData(rc::UniquePtr<IRenderWindowData> data) noexcept { m_data = static_cast<rc::UniquePtr<IRenderWindowData>&&>(data); }

    private:
        GraphicsDevice* m_device;        // borrowed
        IWindow*        m_window;        // borrowed
        rhi::Surface*   m_surface;       // owned
        rhi::SwapChain* m_swapChain;     // owned
        rc::Array<rhi::CommandPool*> m_pools;   // owned, one per frame-in-flight
        rc::Array<rhi::Fence*>       m_fences;  // owned, one per frame-in-flight
        rc::Array<rc::u64>           m_fenceValues;
        rc::u32 m_width;
        rc::u32 m_height;
        rc::UniquePtr<IRenderWindowData> m_data;  // optional typed payload
    };

    class GraphicsDevice
    {
    public:
        // Build a GraphicsDevice from an already-created backend (takes
        // ownership): enumerate adapters, create the logical device + graphics
        // queue. Backend-agnostic — GPU backends (Vulkan/DX12) are built by the
        // `raptor.runtime.graphics.gpu` factory, which then calls this. On failure
        // the backend is destroyed.
        static rc::Result<rc::UniquePtr<GraphicsDevice>> FromBackend(
            rhi::Backend* backend, rc::u32 framesInFlight, const rhi::DeviceFeatures& features = {})
        {
            if (backend == nullptr) { return rc::Err(rc::ErrorCode::Unknown); }

            rc::Span<rhi::Adapter* const> adapters = backend->EnumerateAdapters();
            if (adapters.Size() == 0) { backend->Destroy(); return rc::Err(rc::ErrorCode::Unknown); }

            rhi::DeviceDesc dd{};
            dd.graphicsQueueCount = 1;
            dd.requiredFeatures   = features;
            rhi::Device* device = nullptr;
            if (!adapters[0]->CreateDevice(dd, device).IsOk()) { backend->Destroy(); return rc::Err(rc::ErrorCode::Unknown); }

            rhi::Queue* queue = device->GetQueue(rhi::QueueType::Graphics);
            if (queue == nullptr) { device->Destroy(); backend->Destroy(); return rc::Err(rc::ErrorCode::Unknown); }

            const rc::u32 frames = framesInFlight == 0 ? 1 : framesInFlight;
            auto gd = rc::MakeUnique<GraphicsDevice>(rc::DefaultAllocator(), backend, device, queue, frames);
            return rc::Result<rc::UniquePtr<GraphicsDevice>>(static_cast<rc::UniquePtr<GraphicsDevice>&&>(gd));
        }

        GraphicsDevice(rhi::Backend* backend, rhi::Device* device, rhi::Queue* queue, rc::u32 framesInFlight) noexcept
            : m_backend(backend), m_device(device), m_queue(queue), m_framesInFlight(framesInFlight) {}

        ~GraphicsDevice()
        {
            if (m_device != nullptr) { m_device->WaitIdle(); m_device->Destroy(); m_device = nullptr; }
            if (m_backend != nullptr) { m_backend->Destroy(); m_backend = nullptr; }
        }

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;

        // Create a presentation target (surface + swapchain + per-frame pools/
        // fences) for a window. The window must outlive the RenderWindow.
        rc::Result<rc::UniquePtr<RenderWindow>> CreateRenderWindow(IWindow& window, const RenderWindowDesc& desc)
        {
            const NativeWindow nw = window.Native();
            rhi::Surface* surface = nullptr;
            if (!m_backend->CreateSurface(nw.window, nw.display, surface).IsOk()) { return rc::Err(rc::ErrorCode::Unknown); }

            rhi::SwapChainDesc sd{};
            sd.width       = window.Width();
            sd.height      = window.Height();
            sd.format      = desc.format;
            sd.presentMode = desc.presentMode;
            sd.bufferCount = desc.bufferCount;
            sd.label       = u8"renderwindow";
            rhi::SwapChain* swapChain = nullptr;
            if (!m_device->CreateSwapChain(surface, sd, swapChain).IsOk())
            {
                m_device->DestroySurface(surface);
                return rc::Err(rc::ErrorCode::Unknown);
            }

            rc::Array<rhi::CommandPool*> pools;
            rc::Array<rhi::Fence*> fences;
            for (rc::u32 i = 0; i < m_framesInFlight; ++i)
            {
                rhi::CommandPool* pool = nullptr;
                rhi::Fence* fence = nullptr;
                if (!m_device->CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk() ||
                    !m_device->CreateFence(0, fence).IsOk())
                {
                    for (rhi::CommandPool* p : pools) { m_device->DestroyCommandPool(p); }
                    for (rhi::Fence* f : fences) { m_device->DestroyFence(f); }
                    if (pool != nullptr) { m_device->DestroyCommandPool(pool); }
                    m_device->DestroySwapChain(swapChain);
                    m_device->DestroySurface(surface);
                    return rc::Err(rc::ErrorCode::Unknown);
                }
                pools.PushBack(pool);
                fences.PushBack(fence);
            }

            auto rw = rc::MakeUnique<RenderWindow>(rc::DefaultAllocator(), *this, window, surface, swapChain,
                static_cast<rc::Array<rhi::CommandPool*>&&>(pools),
                static_cast<rc::Array<rhi::Fence*>&&>(fences));
            return rc::Result<rc::UniquePtr<RenderWindow>>(static_cast<rc::UniquePtr<RenderWindow>&&>(rw));
        }

        [[nodiscard]] rhi::Device* Raw() noexcept { return m_device; }
        [[nodiscard]] rhi::Queue*  GfxQueue() noexcept { return m_queue; }
        [[nodiscard]] rc::u32 FramesInFlight() const noexcept { return m_framesInFlight; }
        [[nodiscard]] rc::u32 CurrentFrame() const noexcept { return m_currentFrame; }

        // Advance the CPU frame ring once per app frame (after all windows are
        // rendered). Consumers key per-frame GPU resources on CurrentFrame().
        void AdvanceFrame() noexcept { m_currentFrame = (m_currentFrame + 1) % m_framesInFlight; }

    private:
        rhi::Backend* m_backend;   // owned
        rhi::Device*  m_device;    // owned
        rhi::Queue*   m_queue;     // borrowed from device
        rc::u32 m_framesInFlight;
        rc::u32 m_currentFrame = 0;
    };

    // ----- RenderWindow out-of-line defs (need GraphicsDevice complete) -----

    inline RenderWindow::~RenderWindow()
    {
        rhi::Device* dev = m_device->Raw();
        if (dev != nullptr) { dev->WaitIdle(); }
        for (rhi::CommandPool* p : m_pools) { if (p != nullptr) { dev->DestroyCommandPool(p); } }
        for (rhi::Fence* f : m_fences) { if (f != nullptr) { dev->DestroyFence(f); } }
        if (m_swapChain != nullptr) { dev->DestroySwapChain(m_swapChain); m_swapChain = nullptr; }
        if (m_surface != nullptr) { dev->DestroySurface(m_surface); m_surface = nullptr; }
    }

    inline bool RenderWindow::SyncSize()
    {
        const rc::u32 nw = m_window->Width();
        const rc::u32 nh = m_window->Height();
        if (nw == 0 || nh == 0) { return false; }
        if (nw == m_width && nh == m_height) { return false; }
        m_width = nw;
        m_height = nh;
        m_device->Raw()->WaitIdle();
        m_swapChain->Resize(nw, nh);
        return true;
    }

    inline FrameContext RenderWindow::BeginFrame()
    {
        if (m_window->IsMinimized() || m_window->Width() == 0 || m_window->Height() == 0) { return FrameContext{}; }

        const rc::u32 fi = m_device->CurrentFrame();
        // Guard reuse of this slot's pool/backbuffer: wait the GPU's last
        // submission against this window's fence at this ring slot.
        if (m_fenceValues[fi] > 0) { m_fences[fi]->Wait(m_fenceValues[fi], ~0ull); }

        if (!m_swapChain->AcquireNextImage().IsOk()) { return FrameContext{}; }

        m_pools[fi]->Reset();
        rhi::CommandEncoder* enc = nullptr;
        if (!m_pools[fi]->CreateEncoder(enc).IsOk() || enc == nullptr) { return FrameContext{}; }

        // Host-managed backbuffer transition (Undefined -> RenderTarget).
        enc->TransitionTexture(m_swapChain->CurrentTexture(),
                               rhi::ResourceState::Undefined, rhi::ResourceState::RenderTarget);

        FrameContext f;
        f.valid          = true;
        f.window         = this;
        f.frameIndex     = fi;
        f.width          = m_window->Width();
        f.height         = m_window->Height();
        f.encoder        = enc;
        f.pool           = m_pools[fi];
        f.backbuffer     = m_swapChain->CurrentTexture();
        f.backbufferView = m_swapChain->CurrentTextureView();
        return f;
    }

    inline void RenderWindow::EndFrame(FrameContext& frame)
    {
        if (!frame.valid) { return; }
        const rc::u32 fi = frame.frameIndex;

        frame.encoder->TransitionTexture(m_swapChain->CurrentTexture(),
                                         rhi::ResourceState::RenderTarget, rhi::ResourceState::Present);
        rhi::CommandBuffer* cb = frame.encoder->Finish();

        ++m_fenceValues[fi];
        rhi::CommandBuffer* cbs[1] = { cb };
        m_device->GfxQueue()->Submit(rc::Span<rhi::CommandBuffer* const>(cbs, 1), m_fences[fi], m_fenceValues[fi]);

        m_swapChain->Present(m_device->GfxQueue());
        m_pools[fi]->DestroyEncoder(frame.encoder);
        frame.encoder = nullptr;
    }
}
