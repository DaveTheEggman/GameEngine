/// DXC shader compiler — loads dxcompiler.dll/libdxcompiler.so at runtime.
/// Ported from Sedulous.Shaders/ShaderCompiler.bf via the draconic_port.

module;

#include "DxcIncludes.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

export module raptor.shaders:compiler;

import raptor.core;
import :types;

using namespace raptor::core;

export namespace raptor::shaders {

/// Configuration for compiler creation.
struct CompilerDesc {
    /// Optional override path to the DXC shared library.
    WideStringView dxcompilerPath{};
};

/// HLSL shader compiler backed by DXC (IDxcCompiler3).
struct Compiler {
    void* state = nullptr;

    [[nodiscard]] Status compile(const u8* source, usize sourceSize,
                                 ShaderStage stage, WideStringView entryPoint,
                                 ShaderTarget target, const CompileOptions& options,
                                 CompileResult& out);

    void freeResult(CompileResult& result);
    void Destroy();
};

[[nodiscard]] Status createCompiler(const CompilerDesc& desc, Compiler*& out);

} // namespace raptor::shaders (exported)

// ---- Implementation ----

namespace raptor::shaders {

#ifdef _WIN32
using DynLibHandle = HMODULE;
#else
using DynLibHandle = void*;
#endif

struct CompilerState {
    DynLibHandle            dxcompiler   = nullptr;
    DxcCreateInstanceProc   createInst   = nullptr;
    IDxcCompiler3*          dxc          = nullptr;
    IDxcUtils*              utils        = nullptr;
    IDxcIncludeHandler*     includeHdlr  = nullptr;
};

static CompilerState* stateOf(Compiler* c) { return static_cast<CompilerState*>(c->state); }

// Raptor WideStringView (UTF-16) -> std::wstring for DXC. On Windows wchar_t is
// UTF-16 (copy code units); on Linux wchar_t is UTF-32 (decode surrogate pairs).
static std::wstring widen(WideStringView s) {
    std::wstring out;
    out.reserve(s.Size());
#ifdef _WIN32
    for (usize i = 0; i < s.Size(); ++i) { out.push_back(static_cast<wchar_t>(s.Data()[i])); }
#else
    const char16_t* p   = s.Data();
    const char16_t* end = p + s.Size();
    while (p < end) {
        char32_t cp = *p++;
        if (cp >= 0xD800 && cp <= 0xDBFF && p < end) { // high surrogate
            const char32_t lo = *p++;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
#endif
    return out;
}

static const wchar_t* stagePrefix(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:      return L"vs";
    case ShaderStage::Fragment:    return L"ps";
    case ShaderStage::Compute:     return L"cs";
    case ShaderStage::Mesh:        return L"ms";
    case ShaderStage::Task:        return L"as";
    case ShaderStage::RayGen:
    case ShaderStage::ClosestHit:
    case ShaderStage::AnyHit:
    case ShaderStage::Miss:
    case ShaderStage::Intersection:
    case ShaderStage::Callable:    return L"lib";
    }
    return L"vs";
}

Status Compiler::compile(const u8* source, usize sourceSize,
                         ShaderStage stage, WideStringView entryPoint,
                         ShaderTarget target, const CompileOptions& options,
                         CompileResult& out) {
    auto* s = stateOf(this);
    out = {};

    std::vector<std::wstring> argStorage;
    argStorage.reserve(64);
    auto push  = [&](const wchar_t* a) { argStorage.emplace_back(a); };
    auto pushS = [&](std::wstring a)   { argStorage.emplace_back(std::move(a)); };

    push(L"-E");
    pushS(entryPoint.IsEmpty() ? L"main" : widen(entryPoint));

    std::wstring profile;
    profile.append(stagePrefix(stage));
    profile.append(L"_");
    profile.append(widen(options.shaderModel));
    push(L"-T"); pushS(std::move(profile));

    if (target == ShaderTarget::SPIRV) {
        push(L"-spirv");
        push(L"-fspv-target-env=vulkan1.3");
        for (u32 set = 0; set < options.bindingShiftSets; ++set) {
            wchar_t setBuf[16]; swprintf(setBuf, 16, L"%u", set);
            std::wstring setStr = setBuf;
            auto pushShift = [&](const wchar_t* flag, u32 shift) {
                if (shift == 0) return;
                wchar_t shBuf[16]; swprintf(shBuf, 16, L"%u", shift);
                push(flag); pushS(shBuf); pushS(setStr);
            };
            pushShift(L"-fvk-b-shift", options.bindingShifts.constantBufferShift);
            pushShift(L"-fvk-t-shift", options.bindingShifts.textureShift);
            pushShift(L"-fvk-s-shift", options.bindingShifts.samplerShift);
            pushShift(L"-fvk-u-shift", options.bindingShifts.uavShift);
        }
    }

    push(options.rowMajorMatrices ? L"-Zpr" : L"-Zpc");

    switch (options.optimizationLevel) {
    case 0: push(L"-O0"); break; case 1: push(L"-O1"); break;
    case 2: push(L"-O2"); break; default: push(L"-O3"); break;
    }
    if (options.enableDebugInfo) push(L"-Zi");

    for (usize i = 0; i < options.defines.Size(); ++i) {
        std::wstring arg = L"-D";
        arg.append(widen(options.defines[i].name));
        if (!options.defines[i].value.IsEmpty()) { arg.append(L"="); arg.append(widen(options.defines[i].value)); }
        pushS(std::move(arg));
    }
    for (usize i = 0; i < options.includePaths.Size(); ++i) {
        push(L"-I"); pushS(widen(options.includePaths[i]));
    }
    push(L"-Wno-ignored-attributes");

    std::vector<LPCWSTR> args;
    args.reserve(argStorage.size());
    for (const auto& w : argStorage) args.push_back(w.c_str());

    DxcBuffer src{}; src.Ptr = source; src.Size = sourceSize; src.Encoding = DXC_CP_UTF8;
    IDxcResult* result = nullptr;
    HRESULT hr = s->dxc->Compile(&src, args.data(), static_cast<UINT32>(args.size()), s->includeHdlr, IID_PPV_ARGS(&result));

    if (FAILED(hr) || !result) {
        std::string msg = "IDxcCompiler3::Compile returned HRESULT 0x" + std::to_string(static_cast<unsigned>(hr));
        out.messagesSize = msg.size();
        out.messages = new char[msg.size() + 1];
        std::memcpy(out.messages, msg.data(), msg.size()); out.messages[msg.size()] = '\0';
        return ErrorCode::Unknown;
    }

    IDxcBlobUtf8* errBlob = nullptr; IDxcBlobWide* errName = nullptr;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errBlob), &errName)) && errBlob && errBlob->GetStringLength() > 0) {
        auto n = errBlob->GetStringLength();
        out.messagesSize = n; out.messages = new char[n + 1];
        std::memcpy(out.messages, errBlob->GetStringPointer(), n); out.messages[n] = '\0';
    } else { out.messagesSize = 0; out.messages = new char[1]; out.messages[0] = '\0'; }
    if (errBlob) { errBlob->Release(); }
    if (errName) { errName->Release(); }

    HRESULT status = S_OK; result->GetStatus(&status);
    if (SUCCEEDED(status)) {
        IDxcBlob* objBlob = nullptr; IDxcBlobWide* objName = nullptr;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objBlob), &objName)) && objBlob && objBlob->GetBufferSize() > 0) {
            auto sz = objBlob->GetBufferSize();
            out.bytecodeSize = sz; out.bytecode = new u8[sz];
            std::memcpy(out.bytecode, objBlob->GetBufferPointer(), sz); out.success = true;
        }
        if (objBlob) { objBlob->Release(); }
        if (objName) { objName->Release(); }
    }
    result->Release();
    return out.success ? ErrorCode::Ok : ErrorCode::Unknown;
}

void Compiler::freeResult(CompileResult& r) { delete[] r.bytecode; delete[] r.messages; r = {}; }

void Compiler::Destroy() {
    auto* s = stateOf(this);
    if (!s) return;
    if (s->includeHdlr) { s->includeHdlr->Release(); s->includeHdlr = nullptr; }
    if (s->utils)       { s->utils->Release();        s->utils       = nullptr; }
    if (s->dxc)         { s->dxc->Release();          s->dxc         = nullptr; }
#ifdef _WIN32
    if (s->dxcompiler) { FreeLibrary(s->dxcompiler); s->dxcompiler = nullptr; }
#else
    if (s->dxcompiler) { dlclose(s->dxcompiler);     s->dxcompiler = nullptr; }
#endif
    delete s;
    this->state = nullptr;
}

Status createCompiler(const CompilerDesc& desc, Compiler*& out) {
    out = nullptr;
    auto* c = new Compiler();
    auto* s = new CompilerState();
    c->state = s;

#ifdef _WIN32
    {
        std::wstring wpath;
        if (!desc.dxcompilerPath.IsEmpty()) {
            wpath = widen(desc.dxcompilerPath);
#ifdef DRACO_DXC_PATH
        } else {
            wpath = widen(DRACO_DXC_PATH);
            s->dxcompiler = LoadLibraryW(wpath.c_str());
            if (!s->dxcompiler) wpath = L"dxcompiler.dll";
#else
        } else {
            wpath = L"dxcompiler.dll";
#endif
        }
        if (!s->dxcompiler) s->dxcompiler = LoadLibraryW(wpath.c_str());
    }
    if (!s->dxcompiler) {
        std::fprintf(stderr, "raptor.shaders: LoadLibraryW(dxcompiler.dll) failed (error %lu)\n", GetLastError());
        c->Destroy(); delete c; return ErrorCode::Unknown;
    }
    s->createInst = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(s->dxcompiler, "DxcCreateInstance"));
#else
    {
        std::string path;
        if (desc.dxcompilerPath.IsEmpty()) {
#ifdef RAPTOR_DXC_PATH
            path = RAPTOR_DXC_PATH;
#else
            path = "libdxcompiler.so";
#endif
        } else {
            const String u8 = ToUTF8(desc.dxcompilerPath);
            path.assign(reinterpret_cast<const char*>(u8.CStr()), u8.Size());
        }
        s->dxcompiler = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    }
    if (!s->dxcompiler) {
        std::fprintf(stderr, "raptor.shaders: dlopen(libdxcompiler.so) failed: %s\n", dlerror());
        c->Destroy(); delete c; return ErrorCode::Unknown;
    }
    s->createInst = reinterpret_cast<DxcCreateInstanceProc>(dlsym(s->dxcompiler, "DxcCreateInstance"));
#endif

    if (!s->createInst) { std::fprintf(stderr, "raptor.shaders: DXC library missing DxcCreateInstance\n"); c->Destroy(); delete c; return ErrorCode::Unknown; }
    if (FAILED(s->createInst(CLSID_DxcCompiler, IID_PPV_ARGS(&s->dxc)))) { std::fprintf(stderr, "raptor.shaders: DxcCreateInstance(IDxcCompiler3) failed\n"); c->Destroy(); delete c; return ErrorCode::Unknown; }
    if (FAILED(s->createInst(CLSID_DxcUtils, IID_PPV_ARGS(&s->utils)))) { std::fprintf(stderr, "raptor.shaders: DxcCreateInstance(IDxcUtils) failed\n"); c->Destroy(); delete c; return ErrorCode::Unknown; }
    if (FAILED(s->utils->CreateDefaultIncludeHandler(&s->includeHdlr))) { std::fprintf(stderr, "raptor.shaders: CreateDefaultIncludeHandler failed\n"); c->Destroy(); delete c; return ErrorCode::Unknown; }

    out = c;
    return ErrorCode::Ok;
}

} // namespace raptor::shaders
