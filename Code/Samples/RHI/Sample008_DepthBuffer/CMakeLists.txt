add_executable(Sample008_DepthBuffer Main.cpp)
target_link_libraries(Sample008_DepthBuffer PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample008_DepthBuffer PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample008_DepthBuffer)
