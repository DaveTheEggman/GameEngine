/// Shader feature flags + variant key. Flags are compile-time permutation bits:
/// each set flag becomes a `#define` prepended before compilation, so shaders
/// #ifdef-gate features into specialized, branch-free permutations. Render state
/// (blend/cull/depth) is NOT here — that's PipelineConfig in the material layer.

module;
#include "Core/Prelude.h"   // <new> for placement-new at GCC container instantiation sites

export module draconic.shaders:flags;

import draconic.core;
import :types;

using namespace draconic::core;

export namespace draconic::shaders {

enum class ShaderFlags : u32 {
    None           = 0,
    Skinned        = 1u << 0,   // -> #define SKINNED
    Instanced      = 1u << 1,   // -> #define INSTANCED
    AlphaTest      = 1u << 2,   // -> #define ALPHA_TEST
    NormalMap      = 1u << 3,   // -> #define NORMAL_MAP
    Emissive       = 1u << 4,   // -> #define EMISSIVE
    VertexColors   = 1u << 5,   // -> #define VERTEX_COLORS
    ReceiveShadows = 1u << 6,   // -> #define RECEIVE_SHADOWS
};

[[nodiscard]] constexpr ShaderFlags operator|(ShaderFlags a, ShaderFlags b) noexcept
{
    return static_cast<ShaderFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr ShaderFlags operator&(ShaderFlags a, ShaderFlags b) noexcept
{
    return static_cast<ShaderFlags>(static_cast<u32>(a) & static_cast<u32>(b));
}
constexpr ShaderFlags& operator|=(ShaderFlags& a, ShaderFlags b) noexcept { a = a | b; return a; }
[[nodiscard]] constexpr bool HasFlag(ShaderFlags v, ShaderFlags f) noexcept
{
    return (static_cast<u32>(v) & static_cast<u32>(f)) != 0u;
}

// Append a `#define NAME 1` for each set flag (static-literal names — safe to
// reference for the duration of a compile).
inline void AppendDefines(ShaderFlags flags, Array<ShaderDefine>& out)
{
    if (HasFlag(flags, ShaderFlags::Skinned))        { out.PushBack(ShaderDefine{ u8"SKINNED", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::Instanced))      { out.PushBack(ShaderDefine{ u8"INSTANCED", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::AlphaTest))      { out.PushBack(ShaderDefine{ u8"ALPHA_TEST", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::NormalMap))      { out.PushBack(ShaderDefine{ u8"NORMAL_MAP", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::Emissive))       { out.PushBack(ShaderDefine{ u8"EMISSIVE", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::VertexColors))   { out.PushBack(ShaderDefine{ u8"VERTEX_COLORS", u8"1" }); }
    if (HasFlag(flags, ShaderFlags::ReceiveShadows)) { out.PushBack(ShaderDefine{ u8"RECEIVE_SHADOWS", u8"1" }); }
}

// Stable hash of a shader name (used to key sources + variants without storing
// the name string in every key).
[[nodiscard]] inline u64 ShaderNameHash(StringView name) noexcept
{
    return HashBytes(name.Data(), name.Size());
}

// Identifies one compiled permutation of a named shader. Trivially copyable
// (16 bytes, no padding) so the generic Hash<T> keys it directly.
struct ShaderVariantKey {
    u64         nameHash = 0;
    ShaderStage stage    = ShaderStage::Vertex;
    ShaderFlags flags    = ShaderFlags::None;

    [[nodiscard]] bool operator==(const ShaderVariantKey& o) const noexcept
    {
        return nameHash == o.nameHash && stage == o.stage && flags == o.flags;
    }
};

} // namespace draconic::shaders
