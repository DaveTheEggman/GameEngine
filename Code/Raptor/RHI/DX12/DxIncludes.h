#ifndef DRACO_RHI_DX12_INCLUDES_H_
#define DRACO_RHI_DX12_INCLUDES_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>     // ComPtr
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

// Helper alias.
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#endif // DRACO_RHI_DX12_INCLUDES_H_
