# Draconic::RuntimeGraphics unit tests (doctest, headless via the Null RHI backend).

add_executable(DraconicRuntimeGraphicsTests
    TestsMain.cpp
    GraphicsTests.cpp)

target_link_libraries(DraconicRuntimeGraphicsTests PRIVATE
    Draconic::RuntimeGraphics Draconic::RuntimeGraphicsNull Draconic::ShellNull doctest::doctest)

target_compile_definitions(DraconicRuntimeGraphicsTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRuntimeGraphicsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRuntimeGraphicsTests COMMAND DraconicRuntimeGraphicsTests)
