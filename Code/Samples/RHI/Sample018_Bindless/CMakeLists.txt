add_executable(Sample018_Bindless Main.cpp)
target_link_libraries(Sample018_Bindless PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample018_Bindless PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample018_Bindless)
