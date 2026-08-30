add_executable(Sample013_BorderSampler Main.cpp)
target_link_libraries(Sample013_BorderSampler PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample013_BorderSampler PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample013_BorderSampler)
