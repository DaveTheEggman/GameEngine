// Draconic Core - :path partition
//
// UTF-8 path string manipulation (POSIX '/' separator). Non-owning queries
// return StringView into the input; PathJoin builds a new String.

module;
#include "Core/Prelude.h"

export module draconic.core:path;

import :base;
import :allocator;
import :string;

export namespace draconic::core
{
    inline constexpr utf8char kPathSeparator = utf8char('/');

    [[nodiscard]] inline bool PathIsSeparator(utf8char c) noexcept
    {
        return c == utf8char('/') || c == utf8char('\\');
    }

    [[nodiscard]] inline bool PathIsAbsolute(StringView path) noexcept
    {
        return !path.IsEmpty() && PathIsSeparator(path[0]);
    }

    // The final component (after the last separator).
    [[nodiscard]] inline StringView PathFilename(StringView path) noexcept
    {
        usize start = 0;
        for (usize i = 0; i < path.Size(); ++i)
        {
            if (PathIsSeparator(path[i]))
            {
                start = i + 1;
            }
        }
        return path.SubStr(start, path.Size() - start);
    }

    // The extension including the dot (e.g. ".png"), or empty. A leading-dot
    // filename (".gitignore") has no extension.
    [[nodiscard]] inline StringView PathExtension(StringView path) noexcept
    {
        const StringView name = PathFilename(path);
        usize dot = name.Size();
        for (usize i = 0; i < name.Size(); ++i)
        {
            if (name[i] == utf8char('.'))
            {
                dot = i;
            }
        }
        if (dot == name.Size() || dot == 0)
        {
            return StringView{};
        }
        return name.SubStr(dot, name.Size() - dot);
    }

    // The filename without its extension.
    [[nodiscard]] inline StringView PathStem(StringView path) noexcept
    {
        const StringView name = PathFilename(path);
        const StringView ext = PathExtension(path);
        return name.SubStr(0, name.Size() - ext.Size());
    }

    // Everything before the last separator (empty if there is none).
    [[nodiscard]] inline StringView PathParent(StringView path) noexcept
    {
        usize lastSep = path.Size();
        for (usize i = 0; i < path.Size(); ++i)
        {
            if (PathIsSeparator(path[i]))
            {
                lastSep = i;
            }
        }
        if (lastSep == path.Size())
        {
            return StringView{};
        }
        return path.SubStr(0, lastSep);
    }

    // Joins two paths with a single separator. If `b` is absolute it wins.
    [[nodiscard]] inline String PathJoin(StringView a, StringView b,
                                         IAllocator& allocator = DefaultAllocator())
    {
        if (a.IsEmpty() || PathIsAbsolute(b))
        {
            return String{b, allocator};
        }

        String result{a, allocator};
        if (!PathIsSeparator(a[a.Size() - 1]))
        {
            result.PushBack(kPathSeparator);
        }
        result.Append(b);
        return result;
    }
}
