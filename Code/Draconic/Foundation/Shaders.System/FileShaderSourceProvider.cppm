/// Draconic::ShaderSystem - the `:file_provider` partition.
///
/// The DEV IShaderSourceProvider: engine built-in shaders as real files under the
/// engine shader root (shaders.md P1). Naming convention: the shader NAME is the
/// file stem, the stage is the double extension - `tonemap.ps.hlsl` serves
/// GetVariant("tonemap", Fragment, ...). Shared code lives in `.hlsli` next to
/// them (the root doubles as the DXC include path).
///
/// The manifest is scanned EAGERLY (built-ins are enumerable - tooling wants the
/// list) but sources are read lazily. Hot reload goes through the VFS change
/// source: the whole mount is tracked once (the sweep is recursive), so `.hlsli`
/// edits are seen too - includers are unknown, so a `.hlsli` change reports EVERY
/// shader name (a full recompile is the correct dev answer). The stat sweep is
/// O(files) per Poll, so polls are throttled here, not in callers.

module;
#include "Core/Prelude.h"

export module draconic.shaders.system:file_provider;

import draconic.core;
import draconic.vfs;
import draconic.shaders;
import :shader_system;

namespace core = foundation::core;
namespace vfs = foundation::vfs;

export namespace foundation::shaders
{
    class FileShaderSourceProvider final : public IShaderSourceProvider
    {
    public:
        /// PollChanges is called once per frame; only every Nth call actually stat-sweeps
        /// (the sweep is O(files)). ~1 second at 60 fps - dev hot reload, not a race.
        static constexpr core::u32 PollEveryNCalls = 60;

        /// Mounts `rootDirectory` and scans the manifest. NotFound when the root
        /// does not exist (callers fall back to registered strings, loudly).
        core::Status Initialize(core::StringView rootDirectory)
        {
            if (!core::DirectoryExists(rootDirectory))
            {
                return core::ErrorCode::NotFound;
            }
            m_root = core::String(rootDirectory);
            m_mount = core::MakeUnique<vfs::NativeFileSystem>(core::DefaultAllocator(),
                                                              rootDirectory);

            core::Array<vfs::DirEntry> entries;
            if (!m_mount->Enumerate(u8"", entries).IsOk())
            {
                return core::ErrorCode::Unknown;
            }
            for (const vfs::DirEntry& entry : entries)
            {
                if (entry.isDirectory)
                {
                    continue; // flat root for now; sub-trees can come with growth
                }
                ShaderStage stage;
                core::StringView stem;
                if (!ParseFileName(entry.name.AsView(), stage, stem))
                {
                    continue; // .hlsli and unrelated files
                }
                Entry mapped;
                mapped.name = core::String(stem);
                mapped.stage = stage;
                mapped.fileName = core::String(entry.name.AsView());
                m_entries.PushBack(core::Move(mapped));
            }

            m_changes = m_mount->AsWatchable()->ChangeSource();
            m_changes->Track(u8""); // whole mount, recursive - catches .hlsli too
            return core::ErrorCode::Ok;
        }

        [[nodiscard]] core::StringView RootDirectory() const noexcept { return m_root.AsView(); }
        [[nodiscard]] core::usize ShaderFileCount() const noexcept { return m_entries.Size(); }

        bool FetchSource(core::StringView name, ShaderStage stage,
                         core::String& outSource) override
        {
            for (const Entry& entry : m_entries)
            {
                if (entry.stage == stage && entry.name.AsView() == name)
                {
                    return ReadWholeFile(entry.fileName.AsView(), outSource);
                }
            }
            return false;
        }

        void CollectShaderNames(core::Array<core::String>& out) override
        {
            for (const Entry& entry : m_entries)
            {
                AppendUnique(out, entry.name.AsView());
            }
        }

        bool PollChanges(core::Array<core::String>& outChangedNames) override
        {
            if (m_changes == nullptr)
            {
                return false;
            }
            core::Array<core::String> changedFiles;
            if (!ThrottleElapsed() || !m_changes->Poll(changedFiles))
            {
                return false;
            }
            bool any = false;
            for (const core::String& file : changedFiles)
            {
                if (EndsWith(file.AsView(), u8".hlsli"))
                {
                    // Includers are unknown; reload everything this provider serves.
                    for (const Entry& entry : m_entries)
                    {
                        any = true;
                        AppendUnique(outChangedNames, entry.name.AsView());
                    }
                    continue;
                }
                for (const Entry& entry : m_entries)
                {
                    if (entry.fileName.AsView() == file.AsView())
                    {
                        any = true;
                        AppendUnique(outChangedNames, entry.name.AsView());
                        break;
                    }
                }
            }
            return any;
        }

    private:
        struct Entry
        {
            core::String name;     // shader name = file stem ("tonemap")
            ShaderStage stage;     // from the double extension
            core::String fileName; // mount-relative ("tonemap.ps.hlsl")
        };

        static bool EndsWith(core::StringView text, core::StringView suffix)
        {
            if (text.Size() < suffix.Size())
            {
                return false;
            }
            return text.SubStr(text.Size() - suffix.Size(), suffix.Size()) == suffix;
        }

        static void AppendUnique(core::Array<core::String>& out, core::StringView name)
        {
            for (const core::String& existing : out)
            {
                if (existing.AsView() == name)
                {
                    return;
                }
            }
            out.PushBack(core::String(name));
        }

        /// "tonemap.ps.hlsl" -> (Fragment, "tonemap"); false for anything else.
        static bool ParseFileName(core::StringView fileName, ShaderStage& outStage,
                                  core::StringView& outStem)
        {
            core::StringView suffix;
            if (EndsWith(fileName, u8".vs.hlsl"))
            {
                outStage = ShaderStage::Vertex;
                suffix = u8".vs.hlsl";
            }
            else if (EndsWith(fileName, u8".ps.hlsl"))
            {
                outStage = ShaderStage::Fragment;
                suffix = u8".ps.hlsl";
            }
            else if (EndsWith(fileName, u8".cs.hlsl"))
            {
                outStage = ShaderStage::Compute;
                suffix = u8".cs.hlsl";
            }
            else
            {
                return false;
            }
            outStem = fileName.SubStr(0, fileName.Size() - suffix.Size());
            return !outStem.IsEmpty();
        }

        bool ReadWholeFile(core::StringView fileName, core::String& outSource)
        {
            core::UniquePtr<core::IStream> stream =
                m_mount->Open(fileName, core::FileMode::Read);
            if (!stream)
            {
                return false;
            }
            const core::i64 size = stream->Size();
            if (size < 0)
            {
                return false;
            }
            core::Array<core::u8> bytes;
            bytes.Resize(static_cast<core::usize>(size));
            if (!bytes.IsEmpty() &&
                stream->Read(bytes.Data(), bytes.Size()) != static_cast<core::u64>(bytes.Size()))
            {
                return false;
            }
            outSource = core::String(core::StringView(
                reinterpret_cast<const core::utf8char*>(bytes.Data()), bytes.Size()));
            return true;
        }

        bool ThrottleElapsed()
        {
            if (++m_callsSinceSweep < PollEveryNCalls)
            {
                return false;
            }
            m_callsSinceSweep = 0;
            return true;
        }

        core::String m_root;
        core::UniquePtr<vfs::NativeFileSystem> m_mount;
        vfs::IChangeSource* m_changes = nullptr; // owned by the mount
        core::Array<Entry> m_entries;
        core::u32 m_callsSinceSweep = 0;
    };
}
