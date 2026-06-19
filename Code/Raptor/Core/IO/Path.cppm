// Raptor Core — :path partition
//
// Wide (UTF-16) path string manipulation (POSIX '/' separator). Non-owning
// queries return WideStringView into the input; PathJoin builds a new WideString.
// Paths are wide on the API surface; transcoding to UTF-8 happens at the
// System file API.

module;
#include "Core/Prelude.h"

export module raptor.core:path;

import :base;
import :allocator;
import :string;

export namespace raptor::core
{
    inline constexpr widechar kPathSeparator = u'/';

    [[nodiscard]] inline bool PathIsSeparator(widechar c) noexcept { return c == u'/' || c == u'\\'; }

    [[nodiscard]] inline bool PathIsAbsolute(WideStringView path) noexcept
    {
        return !path.IsEmpty() && PathIsSeparator(path[0]);
    }

    // The final component (after the last separator).
    [[nodiscard]] inline WideStringView PathFilename(WideStringView path) noexcept
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
    [[nodiscard]] inline WideStringView PathExtension(WideStringView path) noexcept
    {
        const WideStringView name = PathFilename(path);
        usize dot = name.Size();
        for (usize i = 0; i < name.Size(); ++i)
        {
            if (name[i] == u'.') { dot = i; }
        }
        if (dot == name.Size() || dot == 0) { return WideStringView{}; }
        return name.SubStr(dot, name.Size() - dot);
    }

    // The filename without its extension.
    [[nodiscard]] inline WideStringView PathStem(WideStringView path) noexcept
    {
        const WideStringView name = PathFilename(path);
        const WideStringView ext = PathExtension(path);
        return name.SubStr(0, name.Size() - ext.Size());
    }

    // Everything before the last separator (empty if there is none).
    [[nodiscard]] inline WideStringView PathParent(WideStringView path) noexcept
    {
        usize lastSep = path.Size();
        for (usize i = 0; i < path.Size(); ++i)
        {
            if (PathIsSeparator(path[i])) { lastSep = i; }
        }
        if (lastSep == path.Size()) { return WideStringView{}; }
        return path.SubStr(0, lastSep);
    }

    // Joins two paths with a single separator. If `b` is absolute it wins.
    [[nodiscard]] inline WideString PathJoin(WideStringView a, WideStringView b, IAllocator& allocator = DefaultAllocator())
    {
        if (a.IsEmpty() || PathIsAbsolute(b)) { return WideString{ b, allocator }; }

        WideString result{ a, allocator };
        if (!PathIsSeparator(a[a.Size() - 1]))
        {
            result.PushBack(kPathSeparator);
        }
        result.Append(b);
        return result;
    }
}
