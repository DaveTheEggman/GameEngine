# Draconic::Physics - `draconic.physics`: the Jolt-backed rigid-body core.
# Jolt is PRIVATE: JPH types never cross the module surface.
add_library(DraconicPhysics STATIC)
add_library(Draconic::Physics ALIAS DraconicPhysics)

target_link_libraries(DraconicPhysics
    PUBLIC Draconic::Core Draconic::Policy
    PRIVATE Draconic::Jolt)

target_sources(DraconicPhysics
    PRIVATE
        WorldImpl.cpp   # module IMPLEMENTATION unit: all Jolt contact (see World.cppm)
    PUBLIC
        FILE_SET physics_modules TYPE CXX_MODULES FILES
            PhysicsModule.cppm
            World.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
