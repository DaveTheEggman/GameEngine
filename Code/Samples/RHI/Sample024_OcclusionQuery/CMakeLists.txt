add_executable(Sample024_OcclusionQuery Main.cpp)
target_link_libraries(Sample024_OcclusionQuery PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample024_OcclusionQuery PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample024_OcclusionQuery)
