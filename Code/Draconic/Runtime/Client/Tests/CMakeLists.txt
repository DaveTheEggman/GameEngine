# Draconic::RuntimeClient unit tests (doctest).

add_executable(DraconicRuntimeClientTests
    TestsMain.cpp
    ApplicationTests.cpp)

target_link_libraries(DraconicRuntimeClientTests PRIVATE
    Draconic::RuntimeClient Draconic::ShellNull Draconic::RuntimeGraphics Draconic::RuntimeGraphicsNull doctest::doctest)

target_compile_definitions(DraconicRuntimeClientTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRuntimeClientTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRuntimeClientTests COMMAND DraconicRuntimeClientTests)
