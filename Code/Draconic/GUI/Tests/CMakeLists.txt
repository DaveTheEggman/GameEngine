# Draconic::GUI unit tests (doctest), added per subsystem as the port lands.
add_executable(DraconicGUITests
    TestsMain.cpp
    RectTests.cpp
    Transform2DTests.cpp
    DrawingTests.cpp
    GradientImageTests.cpp
    CompositorTests.cpp
    TransformableTests.cpp
    NodeTests.cpp)

target_link_libraries(DraconicGUITests PRIVATE
    Draconic::GUI doctest::doctest)

target_compile_definitions(DraconicGUITests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicGUITests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicGUITests COMMAND DraconicGUITests)
