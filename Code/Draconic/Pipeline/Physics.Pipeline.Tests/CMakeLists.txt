
add_executable(DraconicPhysicsPipelineTests TestsMain.cpp PhysicsPipelineTests.cpp PhysicsReflectionTests.cpp)
target_link_libraries(DraconicPhysicsPipelineTests PRIVATE
    Pipeline::Physics Foundation::Physics.Resource Foundation::Physics Pipeline::Core
    Foundation::Geometry Pipeline::Geometry Foundation::Geometry.Resource
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(DraconicPhysicsPipelineTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicPhysicsPipelineTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicPhysicsPipelineTests COMMAND DraconicPhysicsPipelineTests)
