# Foundation::RenderGraph unit tests (doctest).

add_executable(RenderGraph.Tests TestsMain.cpp RenderGraphTests.cpp BarrierTests.cpp
    TypeTests.cpp DescriptorTests.cpp GraphCoreTests.cpp CullingTests.cpp DependencyTests.cpp PassBuilderTests.cpp ValidationTests.cpp DebugTests.cpp ProfilerTests.cpp BundlePassTests.cpp TransientGenerationTests.cpp)
target_link_libraries(RenderGraph.Tests PRIVATE Foundation::RenderGraph Foundation::RHI.Null doctest::doctest)
target_compile_definitions(RenderGraph.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(RenderGraph.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME RenderGraph.Tests COMMAND RenderGraph.Tests)
