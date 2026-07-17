// Draconic::EditorCore - :importer partition.
//
// The file-import seam (asset-pipeline design §7): an OS file (drag-dropped onto the editor)
// becomes a SOURCE - the raw bytes copied into the project's Sources/ tree - plus a typed Asset
// instance in the content DB whose import settings point at it. Cooking then owns the
// source -> product path like any other asset (the imported file's content is part of the
// recipe hash).
//
// IFileImporter implementations live with their asset modules (texture/image/...) and are
// registered by the executable; routing is by lowercase extension. Several importers may claim
// one extension - v1 takes the FIRST match (the Sedulous-style chooser dialog is a later
// nicety; the registry API already exposes all matches).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.core:importer;

import draconic.core;
import draconic.content;
import :project;

using namespace draconic::core;

export namespace draconic::editor
{
    /// Importer-specific options, shown by the import dialog before the import runs. The
    /// dialog renders one checkbox per Toggle (each points into the options object) - a
    /// declarative description, no reflection required. Subclasses add their fields and
    /// return the toggle list; the base is intentionally empty (no options = no dialog).
    class ImportOptions : public ISerializable
    {
        DRACONIC_OBJECT(ImportOptions, ISerializable)
    public:
        struct Toggle
        {
            StringView label;         // checkbox text ("Generate prefab")
            StringView description;   // tooltip (empty = none)
            bool* value = nullptr;    // points into the options object
        };

        [[nodiscard]] virtual Array<Toggle> Toggles() { return {}; }
        void Serialize(ISerializer&) override {}
    };

    class IFileImporter
    {
    public:
        virtual ~IFileImporter() = default;

        /// Shown in menus/choosers ("Texture", "Model", ...).
        [[nodiscard]] virtual StringView Label() const = 0;

        /// Does this importer claim the extension (lowercase, no dot: "png")?
        [[nodiscard]] virtual bool Accepts(StringView extension) const = 0;

        /// Fresh options for one import (defaults set). Null = this importer has no options
        /// and the import runs immediately on drop, no dialog.
        [[nodiscard]] virtual RefPtr<ImportOptions> CreateOptions() const { return {}; }

        /// Import `sourcePath` (absolute OS path): copy the source under Sources/ and create
        /// the typed Asset instance(s) in `group`. Returns the primary created instance.
        /// `options` is the object CreateOptions() returned after the user edited it in the
        /// dialog (null when the importer has none or the import runs headless).
        [[nodiscard]] virtual Result<draconic::content::Instance*> Import(
            StringView sourcePath, EditorProject& project, draconic::content::Group& group,
            const ImportOptions* options = nullptr) = 0;
    };

    DRACONIC_DEFINE_OBJECT(ImportOptions, "draconic::editor")

    class ImporterRegistry
    {
    public:
        void Register(UniquePtr<IFileImporter> importer)
        {
            if (importer) { m_importers.PushBack(Move(importer)); }
        }

        /// First importer claiming the extension (v1 routing), or null.
        [[nodiscard]] IFileImporter* FindFor(StringView extension) const
        {
            for (const UniquePtr<IFileImporter>& importer : m_importers)
            {
                if (importer->Accepts(extension)) { return importer.Get(); }
            }
            return nullptr;
        }

        [[nodiscard]] usize Count() const noexcept { return m_importers.Size(); }

    private:
        Array<UniquePtr<IFileImporter>> m_importers;
    };

    // === shared import helpers ===

    /// Lowercased extension of a path, without the dot ("/a/b/Foo.PNG" -> "png").
    [[nodiscard]] inline String FileExtensionLower(StringView path)
    {
        usize dot = path.Size();
        for (usize i = path.Size(); i > 0; --i)
        {
            const utf8char c = path[i - 1];
            if (c == utf8char('.')) { dot = i; break; }
            if (c == utf8char('/') || c == utf8char('\\')) { break; }
        }
        String ext;
        for (usize i = dot; i < path.Size(); ++i)
        {
            const utf8char c = path[i];
            ext.PushBack((c >= utf8char('A') && c <= utf8char('Z')) ? static_cast<utf8char>(c + 32) : c);
        }
        return ext;
    }

    /// Final path component ("/a/b/foo.png" -> "foo.png").
    [[nodiscard]] inline StringView FileNameOf(StringView path)
    {
        for (usize i = path.Size(); i > 0; --i)
        {
            const utf8char c = path[i - 1];
            if (c == utf8char('/') || c == utf8char('\\')) { return path.SubStr(i, path.Size() - i); }
        }
        return path;
    }

    /// The stem, for instance naming ("foo.png" -> "foo").
    [[nodiscard]] inline StringView FileStemOf(StringView fileName)
    {
        for (usize i = fileName.Size(); i > 0; --i)
        {
            if (fileName[i - 1] == utf8char('.')) { return fileName.SubStr(0, i - 1); }
        }
        return fileName;
    }

    /// Copy an OS file into the project's Sources/ tree. Returns the sources-relative name the
    /// Asset should reference. An existing file of the same name is REUSED (same-source
    /// reimports are the common case); the copy goes through the core file API and the project
    /// path only - the pipeline reads it back through the sources MOUNT.
    [[nodiscard]] inline Result<String> CopyIntoSources(EditorProject& project, StringView sourcePath)
    {
        const StringView fileName = FileNameOf(sourcePath);
        if (fileName.IsEmpty()) { return Err(ErrorCode::InvalidArgument); }
        const String target = PathJoin(project.SourcesRoot().AsView(), fileName);

        if (!FileExists(target.AsView()))
        {
            Result<Array<byte>> bytes = ReadFile(sourcePath);
            if (!bytes.HasValue()) { return Err(bytes.Error()); }
            const Status written = WriteFile(target.AsView(), Span<const byte>(bytes.Value().Data(), bytes.Value().Size()));
            if (!written.IsOk()) { return Err(written.Code()); }
        }
        return String(fileName);
    }
}
