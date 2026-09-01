// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Importer - the `pipeline.importer` module.
//
// The file-import seam: an OS file (drag-dropped onto the editor)
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

export module pipeline.importer;

import foundation.core;
import foundation.content;

using namespace foundation::core;

export namespace pipeline
{
    /// Everything an import needs to know about WHERE it lands - deliberately not the
    /// editor's project object: the pipeline is headless-drivable (CLI, MCP, tests), so the
    /// caller supplies the two facts imports consume and keeps its project model to itself.
    struct ImportContext
    {
        String sourcesRoot; // absolute OS path to the project's Sources/ tree
    };

    /// What kind of asset an import-plan entry would create (the review dialog's grouping).
    enum class ImportResourceKind : u8
    {
        Texture,
        Material,
        Mesh,
        Skeleton,
        AnimationClip,
        Collision,
        Asset, // a single-asset importer's one product (texture/image/audio/font/... file)
    };

    [[nodiscard]] inline StringView ImportResourceKindLabel(ImportResourceKind kind) noexcept
    {
        switch (kind)
        {
        case ImportResourceKind::Texture: return u8"Textures";
        case ImportResourceKind::Material: return u8"Materials";
        case ImportResourceKind::Mesh: return u8"Meshes";
        case ImportResourceKind::Skeleton: return u8"Skeleton";
        case ImportResourceKind::AnimationClip: return u8"Animation Clips";
        case ImportResourceKind::Collision: return u8"Collision";
        case ImportResourceKind::Asset: return u8"Asset";
        }
        return u8"Resources";
    }

    // (SingleAssetPlan - the shared single-asset DescribeImport - is defined below the path
    // helpers it uses.)

    /// One resource an import WOULD create. `sourceName` is the importer's deterministic base
    /// name for the resource (the stable key the commit matches on); `targetName` is what the
    /// user wants it called (defaults to sourceName); unchecked entries are skipped.
    struct ImportPlanEntry
    {
        ImportResourceKind kind = ImportResourceKind::Texture;
        String sourceName;
        String targetName;
        bool enabled = true;
    };

    /// DescribeImport's result: everything the import would create, in fan-out order.
    /// Empty = the importer does not support per-resource review (toggles only).
    struct ImportPlan
    {
        Array<ImportPlanEntry> entries;
        [[nodiscard]] bool IsEmpty() const noexcept { return entries.IsEmpty(); }
        [[nodiscard]] const ImportPlanEntry* Find(ImportResourceKind kind,
                                                 StringView sourceName) const
        {
            for (const ImportPlanEntry& e : entries)
            {
                if (e.kind == kind && e.sourceName.AsView() == sourceName)
                {
                    return &e;
                }
            }
            return nullptr;
        }
    };

    inline void Serialize(ISerializer& ar, ImportPlanEntry& e)
    {
        u8 kind = static_cast<u8>(e.kind);
        foundation::core::Serialize(ar, "kind", kind);
        e.kind = static_cast<ImportResourceKind>(kind);
        foundation::core::Serialize(ar, "sourceName", e.sourceName);
        foundation::core::Serialize(ar, "targetName", e.targetName);
        foundation::core::Serialize(ar, "enabled", e.enabled);
    }

    inline void Serialize(ISerializer& ar, ImportPlan& p)
    {
        foundation::core::Serialize(ar, "entries", p.entries);
    }

    /// Re-import memory: overlay the STORED decisions of a previous import onto a freshly
    /// described plan. Matched entries (kind + sourceName) take the stored enabled state and
    /// rename; resources NEW in the source stay at their described defaults.
    inline void MergeStoredSelection(ImportPlan& plan, const ImportPlan& stored)
    {
        for (ImportPlanEntry& e : plan.entries)
        {
            if (const ImportPlanEntry* s = stored.Find(e.kind, e.sourceName.AsView()))
            {
                e.enabled = s->enabled;
                if (!s->targetName.IsEmpty())
                {
                    e.targetName = s->targetName;
                }
            }
        }
    }

    /// Importer-specific options, shown by the import dialog before the import runs. The
    /// dialog renders one checkbox per Toggle (each points into the options object) - a
    /// declarative description, no reflection required. Subclasses add their fields and
    /// return the toggle list; the base is intentionally empty (no options = no dialog).
    class ImportOptions : public ISerializable
    {
        RTTI_OBJECT(ImportOptions, ISerializable)
    public:
        struct Toggle
        {
            StringView label;       // checkbox text ("Generate prefab")
            StringView description; // tooltip (empty = none)
            bool* value = nullptr;  // points into the options object
        };

        [[nodiscard]] virtual Array<Toggle> Toggles() { return {}; }
        void Serialize(ISerializer&) override {}

        /// The review dialog's per-resource decisions (edited copy of DescribeImport's plan).
        /// Empty = import everything under default names. Importers that support review
        /// consult it via SelectionEnabled/SelectionName at each creation site; others
        /// ignore it.
        ImportPlan selection;

        [[nodiscard]] bool SelectionEnabled(ImportResourceKind kind, StringView sourceName) const
        {
            const ImportPlanEntry* e = selection.Find(kind, sourceName);
            return e == nullptr || e->enabled;
        }
        /// The user's target name for the resource (falls back to the importer's base name).
        [[nodiscard]] StringView SelectionName(ImportResourceKind kind, StringView sourceName) const
        {
            const ImportPlanEntry* e = selection.Find(kind, sourceName);
            return (e != nullptr && !e->targetName.IsEmpty()) ? e->targetName.AsView()
                                                              : sourceName;
        }
    };

    /// A BULK write an importer defers to the worker flush. Three shapes, one struct:
    ///  - data-stream write: `instance` + `streamName` (+ view/owned bytes)
    ///  - envelope write:    `instance` + `object` (SERIALIZATION runs on the worker too -
    ///    a big mesh source rendered to XML is the single most expensive part of an import)
    ///  - raw file copy:     `copyFrom` -> `copyTo` (source + sidecar provenance copies;
    ///    identical bytes skip so mtimes don't churn recooks)
    /// All three are pure file/mount IO with NO in-memory DB mutation, so they are safe off
    /// the UI thread while the job lock excludes cooks and queues deletes. `view` borrows
    /// from the importer's prepared payload (kept alive through the flush).
    struct DeferredImportWrite
    {
        foundation::content::Instance* instance = nullptr; // borrowed; the DB owns it
        RefPtr<ISerializable> object;                    // envelope write when set
        String streamName;                               // data-stream write when set
        Span<const byte> view{};
        Array<byte> owned;
        String copyFrom; // raw copy when both paths set
        String copyTo;

        [[nodiscard]] Span<const byte> Bytes() const noexcept
        {
            return owned.IsEmpty() ? view : Span<const byte>{owned.Data(), owned.Size()};
        }

        /// Execute on the worker. Returns the write's status.
        [[nodiscard]] Status Execute() const
        {
            if (!copyFrom.IsEmpty() && !copyTo.IsEmpty())
            {
                Result<Array<byte>> bytes = ReadFile(copyFrom.AsView());
                if (!bytes.HasValue())
                {
                    return Status{bytes.Error()};
                }
                if (FileExists(copyTo.AsView()))
                {
                    Result<Array<byte>> existing = ReadFile(copyTo.AsView());
                    if (existing.HasValue() && existing.Value().Size() == bytes.Value().Size())
                    {
                        bool same = true;
                        for (usize i = 0; i < bytes.Value().Size(); ++i)
                        {
                            if (existing.Value()[i] != bytes.Value()[i])
                            {
                                same = false;
                                break;
                            }
                        }
                        if (same)
                        {
                            return Status{};
                        }
                    }
                }
                return WriteFile(copyTo.AsView(),
                                 Span<const byte>(bytes.Value().Data(), bytes.Value().Size()));
            }
            if (instance == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            if (object.Get() != nullptr)
            {
                return instance->WriteObject(*object);
            }
            return instance->WriteData(streamName.AsView(), Bytes());
        }

        [[nodiscard]] StringView Label() const noexcept
        {
            if (!copyTo.IsEmpty())
            {
                return copyTo.AsView();
            }
            return (instance != nullptr) ? instance->Name() : StringView(u8"?");
        }
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

        /// Slow importers split in two: PrepareOnWorker runs OFF the UI thread (pure
        /// parse/decode of the source file - NO project or DB access) and its payload is
        /// then handed to Import on the MAIN thread for the fast DB fan-out. Default: no
        /// worker phase (Import does everything inline).
        [[nodiscard]] virtual bool WantsWorkerPrepare() const { return false; }
        [[nodiscard]] virtual RefPtr<Object> PrepareOnWorker(StringView /*sourcePath*/);

        /// Enumerate everything Import would create (the review dialog's data), WITHOUT
        /// touching the project or DB. `prepared` is PrepareOnWorker's payload when the
        /// two-phase path ran (a supporting importer should reuse it, not re-parse).
        /// Default: empty plan = no per-resource review; the dialog shows toggles only.
        [[nodiscard]] virtual ImportPlan DescribeImport(StringView /*sourcePath*/,
                                                        const ImportOptions* /*options*/,
                                                        Object* /*prepared*/)
        {
            return {};
        }

        /// Re-import memory: the selection a PREVIOUS import of this source stored in the
        /// target group (empty = none / unsupported). The editor merges it onto the fresh
        /// plan (MergeStoredSelection) so re-importing does not re-ask settled decisions.
        [[nodiscard]] virtual ImportPlan StoredSelection(foundation::content::Group& /*group*/,
                                                        StringView /*sourcePath*/)
        {
            return {};
        }

        /// Import `sourcePath` (absolute OS path): copy the source under Sources/ and create
        /// the typed Asset instance(s) in `group`. Returns the primary created instance.
        /// `options` is the object CreateOptions() returned after the user edited it in the
        /// dialog (null when the importer has none or the import runs headless). `prepared`
        /// is PrepareOnWorker's payload when the two-phase path ran (null = load inline).
        /// `deferredWrites`: when non-null, the importer MAY park its bulk writes there
        /// instead of writing inline - the caller flushes them on a worker (null =
        /// headless/tests: everything writes inline).
        [[nodiscard]] virtual Result<foundation::content::Instance*>
        Import(StringView sourcePath, const ImportContext& context,
               foundation::content::Group& group, const ImportOptions* options = nullptr,
               Object* prepared = nullptr,
               Array<DeferredImportWrite>* deferredWrites = nullptr) = 0;
    };

    RTTI_DEFINE_OBJECT(ImportOptions, "rtti::pipeline::importer")

    class ImporterRegistry
    {
    public:
        void Register(UniquePtr<IFileImporter> importer);

        /// First importer claiming the extension (v1 routing), or null.
        [[nodiscard]] IFileImporter* FindFor(StringView extension) const;

        /// EVERY importer claiming the extension, in registration order (empty when none). The editor
        /// offers a chooser when more than one matches (e.g. image vs texture on the same extension).
        [[nodiscard]] Array<IFileImporter*> FindAllFor(StringView extension) const;

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
            if (c == utf8char('.'))
            {
                dot = i;
                break;
            }
            if (c == utf8char('/') || c == utf8char('\\'))
            {
                break;
            }
        }
        String ext;
        for (usize i = dot; i < path.Size(); ++i)
        {
            const utf8char c = path[i];
            ext.PushBack((c >= utf8char('A') && c <= utf8char('Z')) ? static_cast<utf8char>(c + 32)
                                                                    : c);
        }
        return ext;
    }

    /// Final path component ("/a/b/foo.png" -> "foo.png").
    [[nodiscard]] inline StringView FileNameOf(StringView path)
    {
        for (usize i = path.Size(); i > 0; --i)
        {
            const utf8char c = path[i - 1];
            if (c == utf8char('/') || c == utf8char('\\'))
            {
                return path.SubStr(i, path.Size() - i);
            }
        }
        return path;
    }

    /// The stem, for instance naming ("foo.png" -> "foo").
    [[nodiscard]] inline StringView FileStemOf(StringView fileName)
    {
        for (usize i = fileName.Size(); i > 0; --i)
        {
            if (fileName[i - 1] == utf8char('.'))
            {
                return fileName.SubStr(0, i - 1);
            }
        }
        return fileName;
    }

    /// The plan of a SINGLE-ASSET importer: one Asset entry named after the file stem - the
    /// shared DescribeImport for texture/image/audio/font/heightfield/splatmap.
    [[nodiscard]] inline ImportPlan SingleAssetPlan(StringView sourcePath)
    {
        ImportPlan plan;
        ImportPlanEntry e;
        e.kind = ImportResourceKind::Asset;
        e.sourceName = String(FileStemOf(FileNameOf(sourcePath)));
        e.targetName = e.sourceName;
        plan.entries.PushBack(Move(e));
        return plan;
    }

    /// The instance name a single-asset importer should CLAIM: the user's rename when a
    /// selection carries one, else the stem.
    [[nodiscard]] inline StringView SingleAssetName(const ImportOptions* options, StringView stem)
    {
        return (options != nullptr) ? options->SelectionName(ImportResourceKind::Asset, stem)
                                    : stem;
    }

    /// Copy an OS file into the project's Sources/ tree. Returns the sources-relative name the
    /// Asset should reference. An existing SAME-CONTENT file is reused untouched; changed
    /// bytes OVERWRITE it (a re-import must see the edited file - the old skip-if-exists
    /// behavior silently kept stale sources). The copy goes through the core file API and the
    /// context's sources root only - the pipeline reads it back through the sources MOUNT.
    [[nodiscard]] inline Result<String> CopyIntoSources(const ImportContext& context,
                                                        StringView sourcePath)
    {
        const StringView fileName = FileNameOf(sourcePath);
        if (fileName.IsEmpty())
        {
            return Err(ErrorCode::InvalidArgument);
        }
        const String target = PathJoin(context.sourcesRoot.AsView(), fileName);

        Result<Array<byte>> bytes = ReadFile(sourcePath);
        if (!bytes.HasValue())
        {
            return Err(bytes.Error());
        }
        if (FileExists(target.AsView()))
        {
            Result<Array<byte>> existing = ReadFile(target.AsView());
            if (existing.HasValue() && existing.Value().Size() == bytes.Value().Size())
            {
                bool same = true;
                for (usize i = 0; i < bytes.Value().Size(); ++i)
                {
                    if (existing.Value()[i] != bytes.Value()[i])
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                {
                    return String(fileName);
                } // identical: no touch, no recook churn
            }
        }
        const Status written = WriteFile(
            target.AsView(), Span<const byte>(bytes.Value().Data(), bytes.Value().Size()));
        if (!written.IsOk())
        {
            return Err(written.Code());
        }
        return String(fileName);
    }
}
