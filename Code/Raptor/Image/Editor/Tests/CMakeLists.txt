if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(RaptorImagePipelineTests TestsMain.cpp ImagePipelineTests.cpp)
target_link_libraries(RaptorImagePipelineTests PRIVATE
    Raptor::ImageEditor Raptor::ImageResource Raptor::Image Raptor::ImageIO
    Raptor::Editor Raptor::Content Raptor::Resource Raptor::VFS doctest::doctest)
target_compile_definitions(RaptorImagePipelineTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(RaptorImagePipelineTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME RaptorImagePipelineTests COMMAND RaptorImagePipelineTests)
