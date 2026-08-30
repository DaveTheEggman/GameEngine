add_executable(Sample026_DynamicOffsets Main.cpp)
target_link_libraries(Sample026_DynamicOffsets PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample026_DynamicOffsets PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample026_DynamicOffsets)
