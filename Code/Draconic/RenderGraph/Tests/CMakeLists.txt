# Draconic::RenderGraph unit tests (doctest).

add_executable(DraconicRenderGraphTests TestsMain.cpp RenderGraphTests.cpp BarrierTests.cpp
    TypeTests.cpp DescriptorTests.cpp GraphCoreTests.cpp CullingTests.cpp DependencyTests.cpp PassBuilderTests.cpp ValidationTests.cpp DebugTests.cpp ProfilerTests.cpp BundlePassTests.cpp TransientGenerationTests.cpp)
target_link_libraries(DraconicRenderGraphTests PRIVATE Draconic::RenderGraph Draconic::RHINull doctest::doctest)
target_compile_definitions(DraconicRenderGraphTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicRenderGraphTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicRenderGraphTests COMMAND DraconicRenderGraphTests)
