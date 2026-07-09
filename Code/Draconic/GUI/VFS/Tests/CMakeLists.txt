add_executable(DraconicGUIVFSTests
    VfsResourceProviderTests.cpp
    TestsMain.cpp)

target_link_libraries(DraconicGUIVFSTests PRIVATE
    Draconic::GUIVFS Draconic::VFS Draconic::Image Draconic::ImageIO doctest::doctest)

target_compile_definitions(DraconicGUIVFSTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicGUIVFSTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicGUIVFSTests COMMAND DraconicGUIVFSTests)
