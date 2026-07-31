# Draconic::Physics - `draconic.physics`: the Jolt-backed rigid-body core.
# Jolt is PRIVATE: JPH types never cross the module surface.
add_library(Draconic.Physics STATIC)
add_library(Draconic::Physics ALIAS Draconic.Physics)

target_link_libraries(Draconic.Physics
    PUBLIC Draconic::Core Draconic::Policy
    PRIVATE Draconic::Jolt)

target_sources(Draconic.Physics
    PRIVATE
        PhysicsWorldImpl.cpp   # module IMPLEMENTATION unit: all Jolt contact (see PhysicsWorld.cppm)
    PUBLIC
        FILE_SET physics_modules TYPE CXX_MODULES FILES
            PhysicsModule.cppm
            PhysicsWorld.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Physics.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
