add_executable(DraconicInputTests
    TestsMain.cpp
    InputTests.cpp)

target_link_libraries(DraconicInputTests PRIVATE
    Draconic::Input Draconic::XmlSerialization Draconic::Settings
    Draconic::Script Draconic::ScriptWren Draconic::InputSubsystem Draconic::Runtime doctest::doctest)

target_compile_definitions(DraconicInputTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicInputTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicInputTests COMMAND DraconicInputTests)
