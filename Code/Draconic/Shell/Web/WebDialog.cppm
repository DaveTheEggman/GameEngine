// Draconic::ShellWeb - `draconic.shell.web:dialogs`.
//
// The web shell's file-dialog service. Stubbed to cancel immediately; the browser equivalents are a
// hidden <input type=file> (open) and an anchor-download (save), wired here later. Kept as the web
// shell's own service so that wiring has a home.

module;
#include "Core/Prelude.h"

export module draconic.shell.web:dialogs;

import draconic.core;
import draconic.shell;

namespace core = draconic::core;

export namespace draconic::shell
{
    class WebDialogService final : public IDialogService
    {
    public:
        void ShowOpenFile(DialogResultCallback callback, core::Span<const FileFilter> = {},
                          core::StringView = {}, bool = false, core::u32 = 0) override
        {
            Cancel(callback);
        }
        void ShowSaveFile(DialogResultCallback callback, core::Span<const FileFilter> = {},
                          core::StringView = {}, core::u32 = 0) override
        {
            Cancel(callback);
        }
        void ShowOpenFolder(DialogResultCallback callback, core::StringView = {}, bool = false,
                            core::u32 = 0) override
        {
            Cancel(callback);
        }
        void OpenPath(core::StringView) override {} // no OS file manager in a browser

    private:
        static void Cancel(DialogResultCallback& callback)
        {
            if (callback)
            {
                callback(core::Span<const core::String>{});
            }
        }
    };
}
