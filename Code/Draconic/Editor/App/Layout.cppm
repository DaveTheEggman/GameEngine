// Draconic::EditorApp - :layout partition.
//
// Per-user dock-layout persistence (docs/design/editor.md §3.2): serialize the toolkit's
// DockLayoutNode snapshot (DockManager::ExportLayout/ApplyLayout) to an XML file in the
// project's Editor/ directory. Panels are matched back by their PersistenceId; unknown ids are
// skipped by ApplyLayout, so a layout file survives panels being added/removed across versions.

module;
#include "Core/Prelude.h"

export module draconic.editor.app:layout;

import draconic.core;
import draconic.vfs;
import draconic.xml.serialization;
import draconic.editor.core;   // EditorContext (favorites persistence)
import draconic.ui.toolkit;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace tk = draconic::ui::toolkit;

    inline constexpr StringView kDockLayoutFile = u8"layout.xml";
    inline constexpr StringView kFavoritesFile = u8"favorites.bin";
    inline constexpr StringView kOpenPagesFile = u8"pages.bin";

    // Bidirectional field walk of one node (children recurse via presence flags).
    inline void SerializeLayoutNode(ISerializer& ar, tk::DockLayoutNode& node)
    {
        ar.BeginObject();
        draconic::core::Serialize(ar, "type", node.Type);
        draconic::core::Serialize(ar, "direction", node.Direction);
        draconic::core::Serialize(ar, "ratio", node.SplitRatio);
        draconic::core::Serialize(ar, "activeTab", node.ActiveTabIndex);
        draconic::core::Serialize(ar, "panels", node.PanelIds);

        bool hasFirst = static_cast<bool>(node.First);
        bool hasSecond = static_cast<bool>(node.Second);
        draconic::core::Serialize(ar, "hasFirst", hasFirst);
        draconic::core::Serialize(ar, "hasSecond", hasSecond);
        if (hasFirst)
        {
            if (ar.Mode() == SerializeMode::Read) { node.First = MakeUnique<tk::DockLayoutNode>(DefaultAllocator()); }
            ar.Key("first");
            SerializeLayoutNode(ar, *node.First);
        }
        if (hasSecond)
        {
            if (ar.Mode() == SerializeMode::Read) { node.Second = MakeUnique<tk::DockLayoutNode>(DefaultAllocator()); }
            ar.Key("second");
            SerializeLayoutNode(ar, *node.Second);
        }
        ar.EndObject();
    }

    // Export `dock`'s current layout to <directory>/<fileName>.
    [[nodiscard]] inline Status SaveDockLayout(tk::DockManager& dock, StringView directory,
                                               StringView fileName = kDockLayoutFile)
    {
        UniquePtr<tk::DockLayoutNode> layout = dock.ExportLayout();
        if (!layout) { return Status{ ErrorCode::NotFound } ; }   // empty dock tree - nothing to save

        MemoryStream buffer;
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(buffer, SerializeMode::Write);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        SerializeLayoutNode(*ctx->serializer, *layout);
        if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }
        ctx->Flush(buffer);

        vfs::NativeFileSystem root(directory);
        vfs::IWritableFileSystem* writable = root.AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }
        return writable->Save(fileName, buffer.Bytes());
    }

    // Rebuild `dock`'s layout from <directory>/<fileName> (panels matched by PersistenceId).
    // NotFound if the file doesn't exist (caller keeps its default layout).
    [[nodiscard]] inline Status LoadDockLayout(tk::DockManager& dock, StringView directory,
                                               StringView fileName = kDockLayoutFile)
    {
        vfs::NativeFileSystem root(directory);
        UniquePtr<IStream> stream = root.Open(fileName, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }

        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(*stream, SerializeMode::Read);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }

        tk::DockLayoutNode layout;
        SerializeLayoutNode(*ctx->serializer, layout);
        if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }

        dock.ApplyLayout(&layout);
        return Status{};
    }
}

// === Favorites persistence (per-user, <project>/Editor/favorites.bin) =======================
namespace draconic::editor::app
{
    [[nodiscard]] inline Status SaveFavorites(draconic::editor::EditorContext& context, StringView directory)
    {
        MemoryStream buffer;
        BinarySerializer ar(buffer, SerializeMode::Write);
        Span<const Guid> favorites = context.Favorites();
        u32 count = static_cast<u32>(favorites.Size());
        draconic::core::Serialize(ar, "count", count);
        for (const Guid& f : favorites) { Guid id = f; ar.Key("id"); ar.GuidValue(id); }
        if (!ar.IsOk()) { return ar.GetStatus(); }

        vfs::NativeFileSystem root(directory);
        vfs::IWritableFileSystem* writable = root.AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }
        return writable->Save(kFavoritesFile, buffer.Bytes());
    }

    [[nodiscard]] inline Status LoadFavorites(draconic::editor::EditorContext& context, StringView directory)
    {
        vfs::NativeFileSystem root(directory);
        UniquePtr<IStream> stream = root.Open(kFavoritesFile, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }
        BinarySerializer ar(*stream, SerializeMode::Read);
        u32 count = 0;
        draconic::core::Serialize(ar, "count", count);
        Array<Guid> favorites;
        for (u32 i = 0; i < count && ar.IsOk(); ++i)
        {
            Guid id;
            ar.Key("id"); ar.GuidValue(id);
            favorites.PushBack(id);
        }
        if (!ar.IsOk()) { return ar.GetStatus(); }
        context.SetFavorites(Move(favorites));
        return Status{};
    }
}

// === Open-page persistence (per-user, <project>/Editor/pages.bin) ===========================
// The page INSTANCE guids from the last session + which one was active. Restored before the
// dock layout so page panels (guid PersistenceIds) land back in their saved arrangement -
// without this only the default document reopened (user-reported: two side-by-side scenes
// became one on restart).
namespace draconic::editor::app
{
    [[nodiscard]] inline Status SaveOpenPages(StringView directory, const Array<Guid>& pages,
                                              const Guid& activePage)
    {
        MemoryStream buffer;
        BinarySerializer ar(buffer, SerializeMode::Write);
        u32 count = static_cast<u32>(pages.Size());
        draconic::core::Serialize(ar, "count", count);
        for (const Guid& id : pages) { Guid guid = id; ar.Key("id"); ar.GuidValue(guid); }
        Guid active = activePage;
        ar.Key("active"); ar.GuidValue(active);
        if (!ar.IsOk()) { return ar.GetStatus(); }

        vfs::NativeFileSystem root(directory);
        vfs::IWritableFileSystem* writable = root.AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }
        return writable->Save(kOpenPagesFile, buffer.Bytes());
    }

    [[nodiscard]] inline Status LoadOpenPages(StringView directory, Array<Guid>& outPages,
                                              Guid& outActivePage)
    {
        vfs::NativeFileSystem root(directory);
        UniquePtr<IStream> stream = root.Open(kOpenPagesFile, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }
        BinarySerializer ar(*stream, SerializeMode::Read);
        u32 count = 0;
        draconic::core::Serialize(ar, "count", count);
        for (u32 i = 0; i < count && ar.IsOk(); ++i)
        {
            Guid id;
            ar.Key("id"); ar.GuidValue(id);
            outPages.PushBack(id);
        }
        ar.Key("active"); ar.GuidValue(outActivePage);
        return ar.IsOk() ? Status{} : ar.GetStatus();
    }
}
