// Foundation::VFS - :data_root partition.
//
// Discovers the DATA ROOT: the "Data" directory that holds the runtime assets, shaders, and
// fonts, identified by a ".dataroot" marker file. Discovery is anchored at the EXECUTABLE
// (robust for a relocated distribution - the working directory is unreliable when an app is
// launched from a menu or a .desktop file), walking up for a dev source tree, then falling
// back to the working directory. One runtime-discovered root replaces per-asset compile-time
// source paths and behaves identically in a dev checkout and a shipped build.
//
// Mounting it as a `data://` scheme is a two-liner over the rest of the VFS:
//     NativeFileSystem dataFs(FindDataRoot());
//     vfs.Mount(u8"data", dataFs);   // then read "data://Assets/..." etc.

module;
#include "Core/Prelude.h"

export module foundation.vfs:data_root;

import foundation.core;

using namespace foundation::core;

export namespace foundation::vfs
{
    // Marker file that identifies a Data root directory.
    inline constexpr StringView kDataRootMarker = u8".dataroot";

    // A directory is a Data root when it holds the marker file.
    [[nodiscard]] inline bool IsDataRoot(StringView dir)
    {
        return FileExists(PathJoin(dir, kDataRootMarker).AsView());
    }

    // Walks up from `startDir`, returning the first "<ancestor>/Data" that is a data root, or
    // empty. The first iteration also covers "<startDir>/Data" - the relocated-dist case where
    // Data/ sits beside the executable.
    [[nodiscard]] inline String FindDataRootFrom(StringView startDir)
    {
        String dir(startDir);
        while (!dir.IsEmpty())
        {
            String candidate = PathJoin(dir.AsView(), u8"Data");
            if (IsDataRoot(candidate.AsView()))
            {
                return candidate;
            }
            dir = String(PathParent(dir.AsView()));
        }
        return String();
    }

    // Finds the absolute path of the data root, or empty if none is found. Search order:
    //   1. <exeDir>/Data, then walk up from <exeDir>   (dist beside the exe; dev under Bin/)
    //   2. <cwd>/Data,    then walk up from <cwd>       (dev convenience)
    [[nodiscard]] inline String FindDataRoot()
    {
        if (String r = FindDataRootFrom(GetExecutableDirectory().AsView()); !r.IsEmpty())
        {
            return r;
        }
        return FindDataRootFrom(GetCurrentDirectory().AsView());
    }

    // Joins a data-root-relative path onto `dataRoot`. An empty root returns `relative`
    // unchanged, so a caller can pass FindDataRoot() straight through and keep a compile-time
    // fallback for the empty case.
    [[nodiscard]] inline String DataPath(StringView dataRoot, StringView relative)
    {
        return dataRoot.IsEmpty() ? String(relative) : PathJoin(dataRoot, relative);
    }
}
