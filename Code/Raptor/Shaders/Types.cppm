/// Shader compilation types.

export module raptor.shaders:types;

import raptor.core;

using namespace raptor::core;

export namespace raptor::shaders {

enum class ShaderStage : u32 {
    Vertex, Fragment, Compute, Mesh, Task,
    RayGen, ClosestHit, AnyHit, Miss, Intersection, Callable,
};

enum class ShaderTarget : u32 {
    SPIRV,
    DXIL,
};

struct ShaderDefine {
    StringView name;
    StringView value;
};

struct BindingShifts {
    u32 constantBufferShift = 0;
    u32 textureShift        = 0;
    u32 samplerShift        = 0;
    u32 uavShift            = 0;
};

struct CompileOptions {
    StringView shaderModel      = u"6_0";
    i32        optimizationLevel = 3;
    bool       enableDebugInfo   = false;
    bool       rowMajorMatrices  = false;
    Span<const ShaderDefine>  defines;
    Span<const StringView>    includePaths;
    BindingShifts bindingShifts;
    u32           bindingShiftSets = 1;
};

struct CompileResult {
    u8*   bytecode      = nullptr;
    usize bytecodeSize  = 0;
    char* messages      = nullptr;
    usize messagesSize  = 0;
    bool  success       = false;
};

} // namespace raptor::shaders
