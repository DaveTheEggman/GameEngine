add_executable(DraconicAudioTests
    TestsMain.cpp
    AudioEngineTests.cpp)

target_link_libraries(DraconicAudioTests PRIVATE
    Draconic::Audio doctest::doctest)

target_compile_definitions(DraconicAudioTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicAudioTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicAudioTests COMMAND DraconicAudioTests)
