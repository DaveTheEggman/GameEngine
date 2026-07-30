// WebGPU headers for the global module fragment (module-partition rule: the GMF may
// contain ONLY #includes). webgpu.h is the STANDARD C header the backend is written
// against; wgpu.h adds wgpu-native's extensions (adapter enumeration, DevicePoll,
// SPIR-V shader ingestion, log callback) - desktop-sidecar-only, never the browser.
#ifndef DRACONIC_RHI_WEBGPU_INCLUDES_H
#define DRACONIC_RHI_WEBGPU_INCLUDES_H

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#endif // DRACONIC_RHI_WEBGPU_INCLUDES_H
