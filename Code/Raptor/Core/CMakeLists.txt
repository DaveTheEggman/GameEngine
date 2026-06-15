# Raptor::Core — the foundation module (`raptor.core`).
#
# One named module composed of partitions (one per subsystem), re-exported by
# CoreModule.cppm. Built as a static library for now.

add_library(RaptorCore STATIC)
add_library(Raptor::Core ALIAS RaptorCore)

target_link_libraries(RaptorCore PUBLIC Raptor::Policy)

# Classic (non-module) translation units: macro-centric subsystems whose
# functions need external linkage reachable from any module's GMF (see
# Debug/Assert.h). Kept minimal.
target_sources(RaptorCore PRIVATE
    Debug/Debug.cpp)

# Module interface units. The primary unit (CoreModule.cppm) must be listed;
# partitions are added here as subsystems land.
target_sources(RaptorCore
    PUBLIC
        FILE_SET core_modules TYPE CXX_MODULES FILES
            CoreModule.cppm
            Base.cppm
            Memory/Memory.cppm
            Memory/SmartPtr.cppm
            System/System.cppm
            Containers/Array.cppm)

# Per-platform System backend (classic TUs).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(RaptorCore PRIVATE System/Linux/LinuxSystem.cpp)
elseif(WIN32)
    target_sources(RaptorCore PRIVATE System/Win32/Win32System.cpp)
endif()

# Classic headers that cross the module boundary (macros only). Consumers
# include them as "Core/<Header>.h"; the engine include root is Code/Raptor.
target_sources(RaptorCore
    PUBLIC
        FILE_SET core_headers TYPE HEADERS
            BASE_DIRS ${CMAKE_SOURCE_DIR}/Code/Raptor
            FILES
                Prelude.h
                Debug/Assert.h
                System/SystemBackend.h)

# So `#include "Core/Prelude.h"` resolves from module global-module-fragments.
target_include_directories(RaptorCore PUBLIC ${CMAKE_SOURCE_DIR}/Code/Raptor)
