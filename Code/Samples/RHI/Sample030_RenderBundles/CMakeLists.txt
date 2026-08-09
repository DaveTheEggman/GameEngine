add_executable(DraconicSample030_RenderBundles Main.cpp)
target_link_libraries(DraconicSample030_RenderBundles PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample030_RenderBundles PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample030_RenderBundles)
