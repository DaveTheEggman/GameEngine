# Raptor::FontsTTF unit tests (doctest). Uses the bundled Roboto font asset.
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(RaptorFontsTTFTests TestsMain.cpp TrueTypeTests.cpp UIFeaturesTests.cpp)
target_link_libraries(RaptorFontsTTFTests PRIVATE Raptor::FontsTTF Raptor::FontsIO Raptor::Fonts doctest::doctest)
target_compile_definitions(RaptorFontsTTFTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    RAPTOR_FONTS_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/fonts")
target_compile_options(RaptorFontsTTFTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME RaptorFontsTTFTests COMMAND RaptorFontsTTFTests)
