// A tiny shared library loaded by the Library (DynamicLibrary) tests.
// Plain C ABI exports so symbol names are unmangled.

#if defined(_WIN32)
    #define RAPTOR_PLUGIN_EXPORT __declspec(dllexport)
#else
    #define RAPTOR_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
    RAPTOR_PLUGIN_EXPORT int RaptorTestAdd(int a, int b) { return a + b; }
    RAPTOR_PLUGIN_EXPORT int RaptorTestAnswer() { return 42; }
}
