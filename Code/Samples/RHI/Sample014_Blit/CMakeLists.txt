add_executable(DraconicSample014_Blit Main.cpp)
target_link_libraries(DraconicSample014_Blit PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample014_Blit PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample014_Blit)
