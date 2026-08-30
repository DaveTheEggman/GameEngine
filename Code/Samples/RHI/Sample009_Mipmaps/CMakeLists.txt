add_executable(Sample009_Mipmaps Main.cpp)
target_link_libraries(Sample009_Mipmaps PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample009_Mipmaps PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample009_Mipmaps)
