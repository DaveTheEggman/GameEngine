// Draconic GUI - `experimental.gui.vfs`: an IResourceProvider backed by a VFS filesystem.
//
// The concrete image loader for the GUI's CSS resource seam: background-image: url(path) resolves
// through this. Kept in a SEPARATE module (like experimental.gui.shell, and mirroring
// foundation.ui.vfs) so the core experimental.gui stays free of a VFS dependency - the app links this
// and hands the provider to a StyleManager. Modeled on foundation.ui.vfs::VfsResourceProvider.
//
// The provider owns the decoded images (m_images), matching the IResourceProvider contract that
// LoadImage returns a borrowed, provider-owned pointer.

module;
#include "Core/Prelude.h"

export module experimental.gui.vfs;

import foundation.core;     // IStream, FileMode, SeekOrigin, Array, Span, UniquePtr, MakeUnique
import foundation.image;    // ImageData, Image, OwnedImageData
import foundation.image.io; // LoadImageFromMemory
import foundation.vfs;      // IFileSystem
import experimental.gui;      // IResourceProvider

using namespace foundation::core;
namespace core = foundation::core;
namespace image = foundation::image;

export namespace experimental::gui::vfs
{
    // IResourceProvider that loads (and decodes) images from a VFS filesystem. The caller owns
    // the filesystem - this provider does not delete it.
    class VfsResourceProvider final : public experimental::gui::IResourceProvider
    {
    public:
        explicit VfsResourceProvider(foundation::vfs::IFileSystem* fs) noexcept : m_fs(fs) {}

        // Load + decode image data at `path`. Returns a borrowed pointer owned by this provider
        // (cached in m_images), or null if the file is missing or fails to decode.
        const image::ImageData* LoadImage(core::StringView path) override
        {
            if (m_fs == nullptr)
                return nullptr;
            UniquePtr<IStream> stream = m_fs->Open(path, FileMode::Read);
            if (!stream)
                return nullptr;

            const i64 length = stream->Size();
            if (length <= 0)
                return nullptr;

            Array<u8> buf;
            buf.Resize(static_cast<usize>(length));
            const u64 read = stream->Read(buf.Data(), static_cast<u64>(length));

            image::Image decoded;
            if (!image::io::LoadImageFromMemory(
                     Span<const u8>(buf.Data(), static_cast<usize>(read)), decoded)
                     .IsOk())
                return nullptr;

            UniquePtr<image::OwnedImageData> owned = MakeUnique<image::OwnedImageData>(
                DefaultAllocator(), decoded.Width(), decoded.Height(), decoded.Format(),
                decoded.PixelData());
            image::OwnedImageData* raw = owned.Get();
            m_images.PushBack(Move(owned));
            return raw;
        }

    private:
        foundation::vfs::IFileSystem* m_fs;
        Array<UniquePtr<image::OwnedImageData>> m_images; // decoded images owned by this provider
    };
}
