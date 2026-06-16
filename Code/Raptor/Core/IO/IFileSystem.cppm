// Raptor Core — :ifilesystem partition
//
// IFileSystem: open streams and test existence by logical path. Implemented by
// NativeFileSystem and VirtualFileSystem.

module;
#include "Core/Prelude.h"

export module raptor.core:ifilesystem;

import :base;
import :unique_ptr;
import :string;
import :system;
import :io;

export namespace raptor::core
{
    class IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        // Opens a stream for the logical path, or null on failure.
        [[nodiscard]] virtual UniquePtr<IStream> Open(StringView path, FileMode mode) = 0;
        [[nodiscard]] virtual bool Exists(StringView path) = 0;
    };
}
