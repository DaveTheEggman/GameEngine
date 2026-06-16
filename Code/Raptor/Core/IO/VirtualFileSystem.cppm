// Raptor Core — :vfs partition
//
// VirtualFileSystem: routes logical paths to mounted IFileSystem backends by
// longest-prefix match. Mounts are non-owning.

module;
#include "Core/Prelude.h"

export module raptor.core:vfs;

import :base;
import :memory;
import :smart_ptr;
import :array;
import :string;
import :io;
import :system;
import :ifilesystem;

export namespace raptor::core
{
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
