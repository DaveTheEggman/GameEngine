add_executable(Sample012_Wireframe Main.cpp)
target_link_libraries(Sample012_Wireframe PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample012_Wireframe PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample012_Wireframe)
