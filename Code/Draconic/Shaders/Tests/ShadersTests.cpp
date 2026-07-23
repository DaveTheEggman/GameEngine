#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cstring>

import draconic.core;
import draconic.shaders;

using namespace draconic::core;
using namespace draconic::shaders;

namespace
{
    constexpr const char* kVertexHlsl = "float4 main(uint id : SV_VertexID) : SV_Position {\n"
                                        "    return float4(0.0, 0.0, 0.0, 1.0);\n"
                                        "}\n";
}

TEST_CASE("shaders: DXC compiles HLSL to SPIR-V")
{
    Compiler* compiler = nullptr;
    if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
    {
        MESSAGE("DXC runtime unavailable; skipping shader compilation test");
        return;
    }

    const auto* source = reinterpret_cast<const u8*>(kVertexHlsl);
    const usize sourceSize = std::strlen(kVertexHlsl);

    CompileResult result{};
    const Status status = compiler->compile(source, sourceSize, ShaderStage::Vertex, u8"main",
                                            ShaderTarget::SPIRV, CompileOptions{}, result);

    CHECK(status.IsOk());
    CHECK(result.success);
    CHECK(result.bytecode != nullptr);
    CHECK(result.bytecodeSize > 0u);
    // SPIR-V magic number (0x07230203) in the first word.
    if (result.bytecode != nullptr && result.bytecodeSize >= 4)
    {
        u32 magic = 0;
        std::memcpy(&magic, result.bytecode, 4);
        CHECK(magic == 0x07230203u);
    }

    compiler->Destroy();
}

TEST_CASE("shaders: a compile error is reported, not a crash")
{
    Compiler* compiler = nullptr;
    if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
    {
        return;
    }

    const char* bad = "this is not valid hlsl @#$";
    CompileResult result{};
    (void)compiler->compile(reinterpret_cast<const u8*>(bad), std::strlen(bad), ShaderStage::Vertex,
                            u8"main", ShaderTarget::SPIRV, CompileOptions{}, result);
    CHECK_FALSE(result.success);

    compiler->Destroy();
}
