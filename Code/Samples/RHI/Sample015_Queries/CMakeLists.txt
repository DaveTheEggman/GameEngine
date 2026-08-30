add_executable(Sample015_Queries Main.cpp)
target_link_libraries(Sample015_Queries PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample015_Queries PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample015_Queries)
