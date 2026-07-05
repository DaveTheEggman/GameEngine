
add_executable(DraconicProfilerTests TestsMain.cpp ProfilerTests.cpp)
target_link_libraries(DraconicProfilerTests PRIVATE Draconic::Profiler doctest::doctest)
target_compile_definitions(DraconicProfilerTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicProfilerTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicProfilerTests COMMAND DraconicProfilerTests)
