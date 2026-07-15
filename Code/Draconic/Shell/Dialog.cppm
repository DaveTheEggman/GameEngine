// Draconic::Shell - :dialog partition.
//
// Native OS file/folder dialogs. Async by nature - the Show* call returns immediately and the
// callback fires later (during the shell's event pump), exactly once, on the main thread - so unlike
// the rest of the shell's pull-based event model these carry a callback. Modeled on Sedulous's
// IDialogService; the desktop backend wraps SDL3's SDL_Show{Open,Save}FileDialog /
// SDL_ShowOpenFolderDialog. A future in-engine file-browser widget can layer on top; native first.

module;
#include "Core/Prelude.h"

export module draconic.shell:dialog;

import draconic.core;

namespace core = draconic::core;

export namespace draconic::shell
{
    // A file-type filter for open/save dialogs. `pattern` is a semicolon-separated list of
    // extensions WITHOUT dots (the SDL form): name = u8"Images", pattern = u8"png;jpg;jpeg".
    // pattern = u8"*" matches everything. Both are borrowed for the duration of the Show* call.
    struct FileFilter
    {
        core::StringView name;
        core::StringView pattern;
    };

    // Delivered once when a dialog resolves. `paths` is empty on cancel or error; otherwise one
    // entry (or several, for a multi-select open). The core::String elements are valid for the
    // duration of the call - copy any you need to keep past it. (Improvement over Sedulous, which
    // hands back views into SDL's transient list.)
    using DialogResultCallback = core::Function<void(core::Span<const core::String>)>;

    // Native file/folder open/save dialogs. Every Show* is async: it returns immediately and
    // `callback` fires later, exactly once. `parentWindowId` (0 = none) makes the dialog modal to
    // that window where the backend supports it. Headless / null backends invoke the callback
    // immediately with an empty result (treated as cancel), so callers need no backend check.
    class IDialogService
    {
    public:
        virtual ~IDialogService() = default;

        virtual void ShowOpenFile(DialogResultCallback callback,
                                  core::Span<const FileFilter> filters = {},
                                  core::StringView defaultPath = {},
                                  bool allowMultiple = false,
                                  core::u32 parentWindowId = 0) = 0;

        virtual void ShowSaveFile(DialogResultCallback callback,
                                  core::Span<const FileFilter> filters = {},
                                  core::StringView defaultPath = {},
                                  core::u32 parentWindowId = 0) = 0;

        virtual void ShowOpenFolder(DialogResultCallback callback,
                                    core::StringView defaultPath = {},
                                    bool allowMultiple = false,
                                    core::u32 parentWindowId = 0) = 0;
    };
}
