// Raptor Core — :path partition
//
// UTF-8 path string manipulation (POSIX '/' separator). Non-owning queries
// return UTF8StringView into the input; PathJoin builds a new UTF8String.
// Paths are UTF-8; reinterpret_cast<const char*>(view.Data()) bridges to the
// System file API when needed.

module;
#include "Core/Prelude.h"

export module raptor.core:path;

import :base;
import :allocator;
import :string;

export namespace raptor::core
{
    inline constexpr utf8char kPathSeparator = u8'/';

    [[nodiscard]] inline bool PathIsSeparator(utf8char c) noexcept { return c == u8'/' || c == u8'\\'; }

    [[nodiscard]] inline bool PathIsAbsolute(UTF8StringView path) noexcept
    {
        return !path.IsEmpty() && PathIsSeparator(path[0]);
    }

    // The final component (after the last separator).
    [[nodiscard]] inline UTF8StringView PathFilename(UTF8StringView path) noexcept
    {
        usize start = 0;
        for (usize i = 0; i < path.Size(); ++i)
        {
            if (PathIsSeparator(path[i])) { start = i + 1; }
        }
        return path.SubStr(start, path.Size() - start);
    }

    // The extension including the dot (e.g. ".png"), or empty. A leading-dot
    // filename (".gitignore") has no extension.
    [[nodiscard]] inline UTF8StringView PathExtension(UTF8StringView path) noexcept
    {
        const UTF8StringView name = PathFilename(path);
        usize dot = name.Size();
        for (usize i = 0; i < name.Size(); ++i)
        {
            if (name[i] == u8'.') { dot = i; }
        }
        if (dot == name.Size() || dot == 0) { return UTF8StringView{}; }
        return name.SubStr(dot, name.Size() - dot);
    }

    // The filename without its extension.
    [[nodiscard]] inline UTF8StringView PathStem(UTF8StringView path) noexcept
    {
        const UTF8StringView name = PathFilename(path);
        const UTF8StringView ext = PathExtension(path);
        return name.SubStr(0, name.Size() - ext.Size());
    }

    // Everything before the last separator (empty if there is none).
    [[nodiscard]] inline UTF8StringView PathParent(UTF8StringView path) noexcept
    {
        usize lastSep = path.Size();
        for (usize i = 0; i < path.Size(); ++i)
        {
            if (PathIsSeparator(path[i])) { lastSep = i; }
        }
        if (lastSep == path.Size()) { return UTF8StringView{}; }
        return path.SubStr(0, lastSep);
    }

    // Joins two paths with a single separator. If `b` is absolute it wins.
    [[nodiscard]] inline UTF8String PathJoin(UTF8StringView a, UTF8StringView b, IAllocator& allocator = DefaultAllocator())
    {
        if (a.IsEmpty() || PathIsAbsolute(b)) { return UTF8String{ b, allocator }; }

        UTF8String result{ a, allocator };
        if (!PathIsSeparator(a[a.Size() - 1]))
        {
            result.PushBack(kPathSeparator);
        }
        result.Append(b);
        return result;
    }
}
