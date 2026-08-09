add_executable(DraconicSample015_Queries Main.cpp)
target_link_libraries(DraconicSample015_Queries PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample015_Queries PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample015_Queries)
