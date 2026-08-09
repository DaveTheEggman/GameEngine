add_executable(DraconicSample026_DynamicOffsets Main.cpp)
target_link_libraries(DraconicSample026_DynamicOffsets PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample026_DynamicOffsets PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample026_DynamicOffsets)
