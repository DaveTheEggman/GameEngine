add_executable(Sample025_MultiDrawIndirect Main.cpp)
target_link_libraries(Sample025_MultiDrawIndirect PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(Sample025_MultiDrawIndirect PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample025_MultiDrawIndirect)
