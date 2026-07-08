# Draconic::Core - the foundation module (`draconic.core`).
#
# One named module composed of partitions (one per subsystem), re-exported by
# CoreModule.cppm. Built as a static library for now.

add_library(DraconicCore STATIC)
add_library(Draconic::Core ALIAS DraconicCore)

target_link_libraries(DraconicCore PUBLIC Draconic::Policy)

# Classic (non-module) translation units: macro-centric subsystems whose
# functions need external linkage reachable from any module's GMF (see
# Debug/Assert.h). Kept minimal.
target_sources(DraconicCore PRIVATE
    Debug/Debug.cpp)

# Module interface units. The primary unit (CoreModule.cppm) must be listed;
# partitions are added here as subsystems land.
target_sources(DraconicCore
    PUBLIC
        FILE_SET core_modules TYPE CXX_MODULES FILES
            CoreModule.cppm
            Base.cppm
            RefCounted.cppm
            UniquePtr.cppm
            Function.cppm
            Memory/Allocator.cppm
            Memory/LinearAllocator.cppm
            Memory/StackAllocator.cppm
            Memory/PoolAllocator.cppm
            Memory/FrameAllocator.cppm
            Memory/TrackingAllocator.cppm
            Memory/MemoryTag.cppm
            System/System.cppm
            System/Time.cppm
            Containers/Span.cppm
            Containers/Array.cppm
            Containers/FixedArray.cppm
            Containers/RingBuffer.cppm
            Containers/IntrusiveList.cppm
            Hash.cppm
            Containers/HashMap.cppm
            Containers/HashSet.cppm
            Text/String.cppm
            Text/Format.cppm
            Log/Logger.cppm
            Log/ConsoleSink.cppm
            Log/FileSink.cppm
            Log/RingLogSink.cppm
            Math/Math.cppm
            Math/Vector2.cppm
            Math/Vector3.cppm
            Math/Vector4.cppm
            Math/Color.cppm
            Random.cppm
            Guid.cppm
            Math/Matrix4.cppm
            Math/Matrix3.cppm
            Math/Quaternion.cppm
            Math/Transform.cppm
            Math/Easings.cppm
            Math/AABB.cppm
            Math/Plane.cppm
            Math/BoundingVolumes.cppm
            Math/Rectangle.cppm
            Math/ContentFit.cppm
            RTTI/TypeInfo.cppm
            RTTI/TypeRegistry.cppm
            RTTI/Object.cppm
            Reflection/Variant.cppm
            Reflection/Instance.cppm
            Reflection/Reflection.cppm
            Reflection/EnumReflection.cppm
            Reflection/ConstantRegistry.cppm
            Reflection/CoreReflection.cppm
            IO/Stream.cppm
            IO/BinaryIO.cppm
            IO/Path.cppm
            IO/FileSystem.cppm
            Serialization/ISerializer.cppm
            Serialization/ISerializable.cppm
            Serialization/SerializableRegistry.cppm
            Serialization/Serializer.cppm
            Serialization/BinarySerializer.cppm
            Serialization/Serialize.cppm
            Threading/Atomic.cppm
            Threading/Thread.cppm
            Threading/Mutex.cppm
            Threading/ScopedLock.cppm
            Threading/SpinLock.cppm
            Threading/ConditionVariable.cppm
            Threading/Semaphore.cppm
            Threading/SharedMutex.cppm
            Threading/JobSystem.cppm
            Library/DynamicLibrary.cppm)

# Per-platform backends (classic TUs).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(DraconicCore PRIVATE
        System/Linux/LinuxSystem.cpp
        Threading/Linux/LinuxThread.cpp)
elseif(WIN32)
    target_sources(DraconicCore PRIVATE
        System/Win32/Win32System.cpp
        Threading/Win32/Win32Thread.cpp)
endif()

# Threading runtime (pthreads on Linux).
find_package(Threads REQUIRED)
target_link_libraries(DraconicCore PUBLIC Threads::Threads)

# Dynamic loader (dlopen/dlsym) for the Library module.
target_link_libraries(DraconicCore PUBLIC ${CMAKE_DL_LIBS})

# Classic headers that cross the module boundary (macros only). Consumers
# include them as "Core/<Header>.h"; the engine include root is Code/Draconic.
target_sources(DraconicCore
    PUBLIC
        FILE_SET core_headers TYPE HEADERS
            BASE_DIRS ${CMAKE_SOURCE_DIR}/Code/Draconic
            FILES
                Prelude.h
                Debug/Assert.h
                System/SystemBackend.h
                Log/Log.h
                Reflection/Reflect.h
                Threading/ThreadBackend.h)

# So `#include "Core/Prelude.h"` resolves from module global-module-fragments.
target_include_directories(DraconicCore PUBLIC ${CMAKE_SOURCE_DIR}/Code/Draconic)
