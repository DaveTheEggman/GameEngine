add_executable(DraconicSample025_MultiDrawIndirect Main.cpp)
target_link_libraries(DraconicSample025_MultiDrawIndirect PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample025_MultiDrawIndirect PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample025_MultiDrawIndirect)
