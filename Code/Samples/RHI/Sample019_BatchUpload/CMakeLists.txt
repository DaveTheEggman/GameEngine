add_executable(Sample019_BatchUpload Main.cpp)
target_link_libraries(Sample019_BatchUpload PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample019_BatchUpload PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample019_BatchUpload)
