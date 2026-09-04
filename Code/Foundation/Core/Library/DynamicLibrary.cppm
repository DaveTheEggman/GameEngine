// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Core - :library partition
//
// DynamicLibrary: an RAII handle over the System raw dynamic-library calls,
// with typed symbol resolution. Foundation for the future plugin/module system
// (discover, load, init/shutdown lifecycle, hot-reload) - S4.9.

module;
#include "Core/Prelude.h"
#include <cstring> // memcpy (avoids the pedantic void*->function-pointer cast)

export module foundation.core:library;

import :base;
import :string;
import :system;

export namespace foundation::core
{
    class DynamicLibrary
    {
    public:
        DynamicLibrary() noexcept = default;
        explicit DynamicLibrary(StringView path) noexcept { m_handle = OpenLibrary(path); }

        DynamicLibrary(DynamicLibrary&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept
        {
            if (this != &other)
            {
                Unload();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        ~DynamicLibrary() { Unload(); }

        Status Load(StringView path) noexcept
        {
            Unload();
            m_handle = OpenLibrary(path);
            return IsLoaded() ? Status{} : Status{ErrorCode::NotFound};
        }

        void Unload() noexcept
        {
            if (m_handle != nullptr)
            {
                CloseLibrary(m_handle);
                m_handle = nullptr;
            }
        }

        // Forget the handle WITHOUT closing the library - it stays mapped for the process
        // lifetime. Hot reload leaks the OLD module on purpose: unreachable stale code is
        // harmless, but freeing pages under any pointer the teardown missed is a crash;
        // the rebuilt module loads from a fresh versioned COPY, so the leaked mapping is
        // never resolved again (game-native-code.md N6).
        void Detach() noexcept { m_handle = nullptr; }

        [[nodiscard]] bool IsLoaded() const noexcept { return m_handle != nullptr; }

        // Resolves a symbol as the requested pointer type (typically a function
        // pointer). Returns nullptr if absent or not loaded.
        template <typename T>
        [[nodiscard]] T GetSymbol(StringView name) const noexcept
        {
            static_assert(sizeof(T) == sizeof(void*), "GetSymbol<T> expects a pointer type.");
            void* symbol = (m_handle != nullptr) ? GetLibrarySymbol(m_handle, name) : nullptr;

            T result{};
            std::memcpy(&result, &symbol, sizeof(result));
            return result;
        }

    private:
        LibraryHandle m_handle = nullptr;
    };
}
