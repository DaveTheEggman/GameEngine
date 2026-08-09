add_executable(Audio.Tests
    TestsMain.cpp
    AudioEngineTests.cpp)

target_link_libraries(Audio.Tests PRIVATE
    Foundation::Audio doctest::doctest)

target_compile_definitions(Audio.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Audio.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Audio.Tests COMMAND Audio.Tests)
