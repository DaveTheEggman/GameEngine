// Raptor::VFS — :native_filesystem partition
//
// NativeFileSystem: backs logical paths with a real directory prefix. Supports
// read, enumerate, and write (the watch capability is not implemented yet).

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
    class NativeFileSystem final
        : public IFileSystem
        , public IEnumerableFileSystem
        , public IWritableFileSystem
    {
    public:
        explicit NativeFileSystem(StringView root, IAllocator& allocator = DefaultAllocator())
            : m_root(root, allocator), m_allocator(&allocator) {}

        // --- IFileSystem ---
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
            return FileExists(full.AsView()) || DirectoryExists(full.AsView());
        }

        [[nodiscard]] IEnumerableFileSystem* AsEnumerable() noexcept override { return this; }
        [[nodiscard]] IWritableFileSystem*   AsWritable()   noexcept override { return this; }

        // --- IEnumerableFileSystem ---
        [[nodiscard]] Status Enumerate(StringView folder, Array<DirEntry>& out) override
        {
            const String full = PathJoin(m_root.AsView(), folder, *m_allocator);
            const bool ok = ListDirectory(
                full.AsView(),
                [](void* ctx, StringView name, bool isDir)
                {
                    auto* dst = static_cast<Array<DirEntry>*>(ctx);
                    dst->PushBack(DirEntry{ String(name), isDir });
                },
                &out);
            return ok ? Status{} : Status{ ErrorCode::NotFound };
        }

        // --- IWritableFileSystem ---
        [[nodiscard]] Status Save(StringView path, Span<const byte> data) override
        {
            const String full = PathJoin(m_root.AsView(), path, *m_allocator);
            EnsureParentDirectories(full.AsView());

            FileStream stream(full.AsView(), FileMode::Write);
            if (!stream.IsValid()) { return Status{ ErrorCode::Internal }; }
            if (!data.IsEmpty() && stream.Write(data.Data(), data.Size()) != data.Size())
            {
                return Status{ ErrorCode::Internal };
            }
            return Status{};
        }

        [[nodiscard]] Status Delete(StringView path) override
        {
            const String full = PathJoin(m_root.AsView(), path, *m_allocator);
            return FileDelete(full.AsView()) ? Status{} : Status{ ErrorCode::NotFound };
        }

    private:
        // Creates every ancestor directory of `full` (idempotent). The final
        // component is the file itself and is left to the caller.
        static void EnsureParentDirectories(StringView full)
        {
            for (usize i = 1; i < full.Size(); ++i)
            {
                if (full[i] == utf8char('/') || full[i] == utf8char('\\'))
                {
                    (void)CreateDirectory(full.SubStr(0, i));
                }
            }
        }

        String m_root;
        IAllocator* m_allocator;
    };
}
