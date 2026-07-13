// Headless tests for the shader variant system: compile-on-demand, flags->defines,
// caching, and invalidation. Compiles real SPIR-V via DXC; creates modules on the
// Null RHI backend (so distinct compiles yield distinct module objects).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.rhi;
import draconic.rhi.null;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
using namespace draconic::shaders;
namespace rhi = draconic::rhi;

namespace
{
    // Fails to compile unless NORMAL_MAP is defined - proves flags->defines apply.
    constexpr const char8_t* kNeedsNormalMap =
        u8"float4 main() : SV_Target {\n"
        u8"#ifndef NORMAL_MAP\n"
        u8"#error NORMAL_MAP required\n"
        u8"#endif\n"
        u8"    return float4(1, 0, 0, 1);\n"
        u8"}\n";

    // Compiles regardless of flags.
    constexpr const char8_t* kTrivialVertex =
        u8"float4 main(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }\n";

    Compiler* MakeCompiler()
    {
        Compiler* c = nullptr;
        if (!createCompiler(CompilerDesc{}, c).IsOk()) { return nullptr; }
        return c;
    }
}

TEST_CASE("shader system: flags become defines; failures aren't cached")
{
    Compiler* compiler = MakeCompiler();
    if (compiler == nullptr) { MESSAGE("DXC unavailable; skipping"); return; }

    rhi::null::NullDevice device{DefaultAllocator()};
    {
        ShaderSystem ss(*compiler, device);
        ss.RegisterSource(u8"guarded", ShaderStage::Fragment, kNeedsNormalMap);

        // Without the flag, NORMAL_MAP is undefined -> compile error -> null (not cached).
        CHECK(ss.GetVariant(u8"guarded", ShaderStage::Fragment, ShaderFlags::None) == nullptr);

        // With NormalMap -> #define NORMAL_MAP -> compiles.
        rhi::ShaderModule* m = ss.GetVariant(u8"guarded", ShaderStage::Fragment, ShaderFlags::NormalMap);
        CHECK(m != nullptr);

        // Same variant is cached (same module object).
        CHECK(ss.GetVariant(u8"guarded", ShaderStage::Fragment, ShaderFlags::NormalMap) == m);

        // A different flag doesn't define NORMAL_MAP -> still fails (specific mapping).
        CHECK(ss.GetVariant(u8"guarded", ShaderStage::Fragment, ShaderFlags::Emissive) == nullptr);

        // Unknown shader / wrong stage -> null.
        CHECK(ss.GetVariant(u8"missing", ShaderStage::Fragment, ShaderFlags::NormalMap) == nullptr);
        CHECK(ss.GetVariant(u8"guarded", ShaderStage::Vertex, ShaderFlags::NormalMap) == nullptr);
    }

    compiler->Destroy();
}

TEST_CASE("shader system: distinct variants cache separately; invalidate recompiles")
{
    Compiler* compiler = MakeCompiler();
    if (compiler == nullptr) { return; }

    rhi::null::NullDevice device{DefaultAllocator()};
    {
        ShaderSystem ss(*compiler, device);
        ss.RegisterSource(u8"vs", ShaderStage::Vertex, kTrivialVertex);

        rhi::ShaderModule* a = ss.GetVariant(u8"vs", ShaderStage::Vertex, ShaderFlags::None);
        rhi::ShaderModule* b = ss.GetVariant(u8"vs", ShaderStage::Vertex, ShaderFlags::Skinned);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a != b);                                                              // different variants
        CHECK(ss.GetVariant(u8"vs", ShaderStage::Vertex, ShaderFlags::None) == a);  // cached

        // Invalidate drops the cached variants; a later request recompiles.
        CHECK(ss.InvalidateShader(u8"vs") == 2u);
        rhi::ShaderModule* a2 = ss.GetVariant(u8"vs", ShaderStage::Vertex, ShaderFlags::None);
        CHECK(a2 != nullptr);
    }

    compiler->Destroy();
}
