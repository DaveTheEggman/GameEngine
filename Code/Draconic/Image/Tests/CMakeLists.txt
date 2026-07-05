add_executable(DraconicImageTests TestsMain.cpp ImageTests.cpp ImageDataTests.cpp ImageManipulationTests.cpp NormalMapTests.cpp)
target_link_libraries(DraconicImageTests PRIVATE Draconic::ImageIO Draconic::Image doctest::doctest)
target_compile_definitions(DraconicImageTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicImageTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicImageTests COMMAND DraconicImageTests)
