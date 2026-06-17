#ifndef DRACO_SHADERS_DXC_INCLUDES_H_
#define DRACO_SHADERS_DXC_INCLUDES_H_

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <Unknwn.h>
#endif

#include <dxc/dxcapi.h>

#endif // DRACO_SHADERS_DXC_INCLUDES_H_
