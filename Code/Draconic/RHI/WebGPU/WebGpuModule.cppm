/// draconic.rhi.webgpu - WebGPU RHI backend (web-platform.md P1).
///
/// Written against the STANDARD webgpu.h; on desktop the implementation is the
/// wgpu-native runtime sidecar (dlopen'd, never linked), on web it is the browser's.
/// Bring-up is staged: device/queue/fence lifecycle is real, resource + command +
/// swapchain factories return honest NotSupported until their stage lands.

export module draconic.rhi.webgpu;

export import :api;
export import :conversions;
export import :buffer;
export import :texture;
export import :texture_view;
export import :sampler;
export import :shader_module;
export import :bind_group_layout;
export import :bind_group;
export import :pipeline_layout;
export import :pipeline_cache;
export import :render_pipeline;
export import :compute_pipeline;
export import :fence;
export import :queue;
export import :device;
export import :adapter;
export import :backend;
