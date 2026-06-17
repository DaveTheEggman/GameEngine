// Raptor::VFS — :native_filesystem partition
//
// NativeFileSystem: backs logical paths with a real directory prefix.

module;
#include "Core/Prelude.h"

export module raptor.vfs:native_filesystem;

import raptor.core;
import :ifilesystem;

using namespace raptor::core;

export namespace raptor::vfs
{
    // =======================================================================
    // NativeFileSystem — backs logical paths with a real directory prefix.
    // =======================================================================
    class NativeFileSystem final : public IFileSystem
    {
    public:
        explicit NativeFileSystem(StringView root, IAllocator& allocator = DefaultAllocator())
            : m_root(root, allocator), m_allocator(&allocator) {}

        [[nodiscard]] UniquePtr<IStream> Open(StringView path, FileMode mode) override
        {
            const String full = PathJoin(m_root.AsView(), path, *m_allocator);
            FileStream* stream = m_allocator->New<FileStream>(full.AsView(), mode);
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

        [[nodiscard]] bool Exists(StringView path) override
        {
            const String full = PathJoin(m_root.AsView(), path, *m_allocator);
            return FileExists(full.AsView());
        }

    private:
        String m_root;
        IAllocator* m_allocator;
    };
}
