// Raptor Core — :native_filesystem partition
//
// NativeFileSystem: backs logical paths with a real directory prefix.

module;
#include "Core/Prelude.h"

export module raptor.core:native_filesystem;

import :base;
import :memory;
import :smart_ptr;
import :string;
import :path;
import :system;
import :io;
import :ifilesystem;

export namespace raptor::core
{
    // =======================================================================
    // NativeFileSystem — backs logical paths with a real directory prefix.
    // =======================================================================
    class NativeFileSystem final : public IFileSystem
    {
    public:
        explicit NativeFileSystem(UTF8StringView root, IAllocator& allocator = DefaultAllocator())
            : m_root(root, allocator), m_allocator(&allocator) {}

        [[nodiscard]] UniquePtr<IStream> Open(UTF8StringView path, FileMode mode) override
        {
            const UTF8String full = PathJoin(m_root.AsView(), path, *m_allocator);
            FileStream* stream = m_allocator->New<FileStream>(
                reinterpret_cast<const char*>(full.CStr()), mode);
            if (stream == nullptr)
            {
                return UniquePtr<IStream>{};
            }
            if (!stream->IsValid())
            {
                m_allocator->Delete(stream);
                return UniquePtr<IStream>{};
            }
            return UniquePtr<IStream>{ stream, *m_allocator };
        }

        [[nodiscard]] bool Exists(UTF8StringView path) override
        {
            const UTF8String full = PathJoin(m_root.AsView(), path, *m_allocator);
            return FileExists(reinterpret_cast<const char*>(full.CStr()));
        }

    private:
        UTF8String m_root;
        IAllocator* m_allocator;
    };
}
