if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(DraconicVGSVGTests TestsMain.cpp
    SVGColorParserTests.cpp SVGTransformParserTests.cpp SVGPathParserTests.cpp
    SVGLoaderTests.cpp)
target_link_libraries(DraconicVGSVGTests PRIVATE Draconic::VGSVG Draconic::VG Draconic::Core doctest::doctest)
target_compile_definitions(DraconicVGSVGTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVGSVGTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVGSVGTests COMMAND DraconicVGSVGTests)
