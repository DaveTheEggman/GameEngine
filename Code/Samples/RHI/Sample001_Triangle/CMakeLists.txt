add_executable(Sample001_Triangle Main.cpp)
target_link_libraries(Sample001_Triangle PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample001_Triangle PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample001_Triangle)
