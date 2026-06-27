/// Raptor::ShaderSystem — the `raptor.shaders.system` module.
///
/// Compile-on-demand + cache for shader VARIANTS. A shader is registered by name
/// per stage (its HLSL source); GetVariant(name, stage, flags) compiles the
/// permutation (flags -> #defines) via DXC, creates the GPU ShaderModule, and
/// caches it by (nameHash, stage, flags). This is the layer above the stateless
/// raptor.shaders Compiler that the material/PSO layers build on. Needs the RHI
/// to create modules, so it's separate from the RHI-free raptor.shaders.

module;
#include "Core/Prelude.h"

export module raptor.shaders.system;

import raptor.core;
import raptor.rhi;
import raptor.shaders;

namespace rc = raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::shaders {

// Compile-on-demand variant cache. The Compiler and Device are borrowed (owned by
// the caller). Sources are registered per (name, stage) — vertex and fragment are
// separate HLSL with `main` entry points (as in the Sedulous shader set).
class ShaderSystem {
public:
    ShaderSystem(Compiler& compiler, rhi::Device& device) noexcept
        : m_compiler(&compiler), m_device(&device) {}

    ~ShaderSystem() { DestroyAll(); }

    ShaderSystem(const ShaderSystem&) = delete;
    ShaderSystem& operator=(const ShaderSystem&) = delete;

    // Register a shader's HLSL source for a stage (owned copy).
    void RegisterSource(rc::StringView name, ShaderStage stage, rc::StringView hlsl)
    {
        m_sources.InsertOrAssign(SourceKey(name, stage), rc::String(hlsl));
    }

    // Include search paths for DXC #include resolution of shared .hlsli (owned).
    void SetIncludePaths(rc::Span<const rc::StringView> paths)
    {
        m_includePaths.Clear();
        for (rc::usize i = 0; i < paths.Size(); ++i) { m_includePaths.PushBack(rc::String(paths[i])); }
    }

    // Get (compile-on-demand + cache) the GPU module for a variant. Returns null
    // if the source is unknown or compilation fails (failures are NOT cached, so a
    // later request retries — e.g. after a fix).
    [[nodiscard]] rhi::ShaderModule* GetVariant(rc::StringView name, ShaderStage stage, ShaderFlags flags)
    {
        const ShaderVariantKey key{ ShaderNameHash(name), stage, flags };
        if (rhi::ShaderModule** cached = m_cache.Find(key)) { return *cached; }

        rc::String* source = m_sources.Find(SourceKey(name, stage));
        if (source == nullptr) { return nullptr; }

        rhi::ShaderModule* module = Compile(source->AsView(), stage, flags);
        if (module == nullptr) { return nullptr; }

        m_cache.InsertOrAssign(key, module);
        return module;
    }

    // Drop + destroy every cached variant of a shader and BUMP its version (the
    // reload signal consumers poll). Call on a shader reload. The next GetVariant
    // recompiles. Returns how many variants were invalidated.
    rc::usize InvalidateShader(rc::StringView name)
    {
        const rc::u64 nameHash = ShaderNameHash(name);
        rc::Array<ShaderVariantKey> toRemove;
        for (auto& e : m_cache)
        {
            if (e.key.nameHash == nameHash)
            {
                if (e.value != nullptr) { m_device->DestroyShaderModule(e.value); }
                toRemove.PushBack(e.key);
            }
        }
        for (const ShaderVariantKey& k : toRemove) { m_cache.Remove(k); }
        BumpVersion(nameHash);
        return toRemove.Size();
    }

    // Monotonic version of a shader: bumped each InvalidateShader (i.e. each
    // reload). The PSO cache stamps pipelines with this and rebuilds when it
    // changes. 0 if the shader was never registered/invalidated.
    [[nodiscard]] rc::u64 Version(rc::StringView name) noexcept
    {
        rc::u64* v = m_versions.Find(ShaderNameHash(name));
        return (v != nullptr) ? *v : 0ull;
    }

private:
    [[nodiscard]] rhi::ShaderModule* Compile(rc::StringView source, ShaderStage stage, ShaderFlags flags)
    {
        const bool isDX12 = (m_device->type == rhi::DeviceType::DX12);
        const ShaderTarget target = isDX12 ? ShaderTarget::DXIL : ShaderTarget::SPIRV;

        rc::Array<ShaderDefine> defines;
        AppendDefines(flags, defines);

        rc::Array<rc::StringView> includeViews;
        for (const rc::String& p : m_includePaths) { includeViews.PushBack(p.AsView()); }

        CompileOptions opts{};
        opts.shaderModel       = u8"6_0";
        opts.optimizationLevel = 3;
        opts.defines           = rc::Span<const ShaderDefine>(defines.Data(), defines.Size());
        opts.includePaths      = rc::Span<const rc::StringView>(includeViews.Data(), includeViews.Size());
        if (!isDX12)
        {
            // Vulkan: shift register spaces so HLSL b/t/u/s registers don't collide
            // in SPIR-V (matches the sample framework's CompileToModule).
            opts.bindingShifts.constantBufferShift = 0;
            opts.bindingShifts.textureShift        = 1000;
            opts.bindingShifts.uavShift            = 2000;
            opts.bindingShifts.samplerShift        = 3000;
            opts.bindingShiftSets                  = 4;
        }

        CompileResult cr{};
        const rc::Status r = m_compiler->compile(
            reinterpret_cast<const rc::u8*>(source.Data()), source.Size(),
            stage, u8"main", target, opts, cr);

        if (r != rc::ErrorCode::Ok || !cr.success)
        {
            if (cr.messages != nullptr) { rhi::LogErrorf("Shader variant compile failed: %s", cr.messages); }
            m_compiler->freeResult(cr);
            return nullptr;
        }

        rhi::ShaderModuleDesc desc{};
        desc.code = rc::Span<const rc::u8>(cr.bytecode, cr.bytecodeSize);
        rhi::ShaderModule* module = nullptr;
        const rc::Status mr = m_device->CreateShaderModule(desc, module);
        m_compiler->freeResult(cr);
        return (mr == rc::ErrorCode::Ok) ? module : nullptr;
    }

    void DestroyAll()
    {
        for (auto& e : m_cache) { if (e.value != nullptr) { m_device->DestroyShaderModule(e.value); } }
        m_cache.Clear();
    }

    [[nodiscard]] static rc::u64 SourceKey(rc::StringView name, ShaderStage stage) noexcept
    {
        return (ShaderNameHash(name) * 1099511628211ull) ^ static_cast<rc::u64>(stage);
    }

    void BumpVersion(rc::u64 nameHash)
    {
        rc::u64* v = m_versions.Find(nameHash);
        if (v == nullptr) { m_versions.InsertOrAssign(nameHash, 0ull); v = m_versions.Find(nameHash); }
        ++(*v);
    }

    Compiler*    m_compiler;   // borrowed
    rhi::Device* m_device;     // borrowed
    rc::HashMap<rc::u64, rc::String> m_sources;                  // (name,stage) -> HLSL
    rc::HashMap<ShaderVariantKey, rhi::ShaderModule*> m_cache;   // variant -> GPU module (owned)
    rc::HashMap<rc::u64, rc::u64> m_versions;                    // nameHash -> version
    rc::Array<rc::String> m_includePaths;
};

} // namespace raptor::shaders
