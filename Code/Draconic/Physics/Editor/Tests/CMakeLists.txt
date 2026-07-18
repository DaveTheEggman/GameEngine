
add_executable(DraconicPhysicsPipelineTests TestsMain.cpp PhysicsPipelineTests.cpp)
target_link_libraries(DraconicPhysicsPipelineTests PRIVATE
    Draconic::PhysicsEditor Draconic::PhysicsResource Draconic::Physics Draconic::Editor
    Draconic::Geometry Draconic::MeshEditor Draconic::MeshResource
    Draconic::Content Draconic::Resource Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicPhysicsPipelineTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicPhysicsPipelineTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicPhysicsPipelineTests COMMAND DraconicPhysicsPipelineTests)
