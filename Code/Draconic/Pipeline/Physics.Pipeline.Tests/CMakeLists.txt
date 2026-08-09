
add_executable(DraconicPhysicsPipelineTests TestsMain.cpp PhysicsPipelineTests.cpp PhysicsReflectionTests.cpp)
target_link_libraries(DraconicPhysicsPipelineTests PRIVATE
    Draconic::Physics.Pipeline Draconic::Physics.Resource Draconic::Physics Draconic::Pipeline.Core
    Draconic::Geometry Draconic::Geometry.Pipeline Draconic::Geometry.Resource
    Draconic::Content Draconic::Resource Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicPhysicsPipelineTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicPhysicsPipelineTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicPhysicsPipelineTests COMMAND DraconicPhysicsPipelineTests)
