add_executable(Sample030_RenderBundles Main.cpp)
target_link_libraries(Sample030_RenderBundles PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample030_RenderBundles PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample030_RenderBundles)
