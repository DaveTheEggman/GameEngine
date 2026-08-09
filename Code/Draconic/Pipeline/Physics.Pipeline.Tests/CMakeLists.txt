
add_executable(Physics.Pipeline.Tests TestsMain.cpp PhysicsPipelineTests.cpp PhysicsReflectionTests.cpp)
target_link_libraries(Physics.Pipeline.Tests PRIVATE
    Pipeline::Physics Foundation::Physics.Resource Foundation::Physics Pipeline::Core
    Foundation::Geometry Pipeline::Geometry Foundation::Geometry.Resource
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Physics.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Physics.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Physics.Pipeline.Tests COMMAND Physics.Pipeline.Tests)
