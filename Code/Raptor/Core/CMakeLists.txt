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
            Containers/Span.cppm
            Containers/Array.cppm
            Containers/RingBuffer.cppm
            Containers/IntrusiveList.cppm
            Containers/Hash.cppm
            Containers/HashMap.cppm
            Containers/HashSet.cppm
            Text/String.cppm
            Text/Format.cppm
            Log/Log.cppm
            Math/Math.cppm
            Math/Color.cppm
            Math/Random.cppm
            Math/Matrix.cppm
            Math/Geometry.cppm
            RTTI/RTTI.cppm
            RTTI/Variant.cppm
            RTTI/Reflection.cppm
            IO/Stream.cppm
            IO/Path.cppm
            IO/FileSystem.cppm
            IO/IFileSystem.cppm
            IO/NativeFileSystem.cppm
            IO/VirtualFileSystem.cppm
            Serialization/ISerializer.cppm
            Serialization/Serializer.cppm
            Serialization/BinarySerializer.cppm
            Serialization/Serialize.cppm
            Threading/Threading.cppm
            Library/DynamicLibrary.cppm)

# Per-platform backends (classic TUs).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(RaptorCore PRIVATE
        System/Linux/LinuxSystem.cpp
        Threading/Linux/LinuxThread.cpp)
elseif(WIN32)
    target_sources(RaptorCore PRIVATE
        System/Win32/Win32System.cpp
        Threading/Win32/Win32Thread.cpp)
endif()

# Threading runtime (pthreads on Linux).
find_package(Threads REQUIRED)
target_link_libraries(RaptorCore PUBLIC Threads::Threads)

# Dynamic loader (dlopen/dlsym) for the Library module.
target_link_libraries(RaptorCore PUBLIC ${CMAKE_DL_LIBS})

# Classic headers that cross the module boundary (macros only). Consumers
# include them as "Core/<Header>.h"; the engine include root is Code/Raptor.
target_sources(RaptorCore
    PUBLIC
        FILE_SET core_headers TYPE HEADERS
            BASE_DIRS ${CMAKE_SOURCE_DIR}/Code/Raptor
            FILES
                Prelude.h
                Debug/Assert.h
                System/SystemBackend.h
                Log/Log.h
                RTTI/Reflect.h
                Threading/ThreadBackend.h)

# So `#include "Core/Prelude.h"` resolves from module global-module-fragments.
target_include_directories(RaptorCore PUBLIC ${CMAKE_SOURCE_DIR}/Code/Raptor)
