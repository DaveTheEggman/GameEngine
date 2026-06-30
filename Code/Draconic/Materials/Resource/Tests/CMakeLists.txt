# Draconic::MaterialResource unit tests (doctest; real DXC + Null RHI + content DB).
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
        SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(DraconicMaterialResourceTests
    TestsMain.cpp
    MaterialResourceTests.cpp)

target_link_libraries(DraconicMaterialResourceTests PRIVATE
    Draconic::MaterialResource Draconic::ShaderSystem Draconic::RHINull Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicMaterialResourceTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicMaterialResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicMaterialResourceTests COMMAND DraconicMaterialResourceTests)
