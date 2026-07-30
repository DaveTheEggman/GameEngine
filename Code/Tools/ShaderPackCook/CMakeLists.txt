# RaptorShaderPackCook - headless CLI that cooks the engine shader corpus into a shaders.dpak
# (the WGSL pack the web build ships). Needs DXC + the vendored naga/tint (baked into
# draconic.shaders), so it is a desktop authoring tool - never built for Emscripten.
if(EMSCRIPTEN)
    return()
endif()

add_executable(RaptorShaderPackCook main.cpp)
target_link_libraries(RaptorShaderPackCook PRIVATE
    Draconic::Core
    Draconic::Shaders
    Draconic::Policy)
