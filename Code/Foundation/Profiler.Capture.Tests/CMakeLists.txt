
add_executable(Profiler.Capture.Tests TestsMain.cpp ChromeTraceTests.cpp CaptureRecorderTests.cpp)
target_link_libraries(Profiler.Capture.Tests PRIVATE Foundation::Profiler.Capture doctest::doctest)
target_compile_definitions(Profiler.Capture.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Profiler.Capture.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Profiler.Capture.Tests COMMAND Profiler.Capture.Tests)
