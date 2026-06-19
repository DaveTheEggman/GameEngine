if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(RaptorTextureFactoryTests TestsMain.cpp TextureFactoryTests.cpp)
target_link_libraries(RaptorTextureFactoryTests PRIVATE
    Raptor::TextureResource Raptor::Texture Raptor::RHI Raptor::RHINull
    Raptor::Content Raptor::Resource Raptor::VFS doctest::doctest)
target_compile_definitions(RaptorTextureFactoryTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(RaptorTextureFactoryTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME RaptorTextureFactoryTests COMMAND RaptorTextureFactoryTests)
