// Raptor Core — :vfs partition
//
// A virtual file system: open streams by logical path. NativeFileSystem maps a
// logical root onto a real directory; VirtualFileSystem routes logical paths to
// mounted backends by longest-prefix match. Mounts are non-owning.

module;
#include "Core/Prelude.h"

export module raptor.core:vfs;

import :base;
import :memory;
import :smart_ptr;
import :array;
import :string;
import :path;
import :system;
import :io;

export namespace raptor::core
{
    class IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        // Opens a stream for the logical path, or null on failure.
        [[nodiscard]] virtual UniquePtr<IStream> Open(UTF8StringView path, FileMode mode) = 0;
        [[nodiscard]] virtual bool Exists(UTF8StringView path) = 0;
    };

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

    // =======================================================================
    // VirtualFileSystem — routes logical paths to mounted backends.
    // =======================================================================
    class VirtualFileSystem final : public IFileSystem
    {
    public:
        // Mounts a backend under a logical prefix (non-owning; backend must outlive this).
        void Mount(UTF8StringView prefix, IFileSystem& backend)
        {
            m_mounts.PushBack(MountPoint{ UTF8String(prefix), &backend });
        }

        [[nodiscard]] UniquePtr<IStream> Open(UTF8StringView path, FileMode mode) override
        {
            const MountPoint* mount = FindMount(path);
            if (mount == nullptr)
            {
                return UniquePtr<IStream>{};
            }
            return mount->backend->Open(Relative(path, *mount), mode);
        }

        [[nodiscard]] bool Exists(UTF8StringView path) override
        {
            const MountPoint* mount = FindMount(path);
            return mount != nullptr && mount->backend->Exists(Relative(path, *mount));
        }

    private:
        struct MountPoint
        {
            UTF8String prefix;
            IFileSystem* backend;
        };

        [[nodiscard]] const MountPoint* FindMount(UTF8StringView path) const
        {
            const MountPoint* best = nullptr;
            for (const MountPoint& mount : m_mounts)
            {
                if (path.StartsWith(mount.prefix.AsView()))
                {
                    if (best == nullptr || mount.prefix.Size() > best->prefix.Size())
                    {
                        best = &mount;
                    }
                }
            }
            return best;
        }

        [[nodiscard]] static UTF8StringView Relative(UTF8StringView path, const MountPoint& mount)
        {
            usize offset = mount.prefix.Size();
            while (offset < path.Size() && (path[offset] == u8'/' || path[offset] == u8'\\'))
            {
                ++offset;
            }
            return path.SubStr(offset, path.Size() - offset);
        }

        Array<MountPoint> m_mounts;
    };
}
