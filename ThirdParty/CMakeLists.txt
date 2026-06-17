# Raptor third-party dependencies.

# DXC — vendored headers + pre-built runtime binaries (HLSL -> SPIR-V/DXIL).
# Headers are an INTERFACE target; the runtime library is loaded at run time
# (dlopen / LoadLibrary), so we only publish its path + an rpath for dev builds.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/DXC/include/dxc/dxcapi.h)
    add_library(raptor_dxc_headers INTERFACE)
    add_library(Raptor::DXCHeaders ALIAS raptor_dxc_headers)
    target_include_directories(raptor_dxc_headers
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/DXC/include)

    if(WIN32)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/win-x64")
        if(EXISTS "${_dxc_lib_dir}/dxcompiler.dll")
            target_compile_definitions(raptor_dxc_headers
                INTERFACE RAPTOR_DXC_PATH="${_dxc_lib_dir}/dxcompiler.dll")
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    elseif(UNIX AND NOT APPLE)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/linux-x86_64")
        if(EXISTS "${_dxc_lib_dir}/libdxcompiler.so")
            target_compile_definitions(raptor_dxc_headers
                INTERFACE RAPTOR_DXC_PATH="${_dxc_lib_dir}/libdxcompiler.so")
            list(APPEND CMAKE_BUILD_RPATH "${_dxc_lib_dir}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" PARENT_SCOPE)
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    endif()
else()
    message(STATUS "ThirdParty/DXC not present — raptor.shaders will be skipped.")
endif()
