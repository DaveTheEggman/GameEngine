// Raptor::Content — the `raptor.content` module.
//
// A content database: a hierarchical store of serializable objects, addressed by
// Guid (stable) or by path. A Group is a folder; an Instance is one stored unit
// (a Guid + a primary ISerializable object + named data streams for heavy
// blobs). Backed by a VFS mount (Group = directory, Instance = a ".rasset"
// envelope file, data streams = sidecar files). Identity is decoupled from byte
// access: the database owns the Guid<->location structure; the VFS owns the
// bytes.
//
// Sits between Core/VFS and the resource layer; consumes ISerializable +
// SerializableRegistry (polymorphic construct-by-type) + the type registry.

module;
#include "Core/Prelude.h"

export module raptor.content;

import raptor.core;
import raptor.vfs;

using namespace raptor::core;
using namespace raptor::vfs;

export namespace raptor::content
{
    inline constexpr u32 kEnvelopeMagic   = 0x54534152u; // 'RAST'
    inline constexpr u32 kEnvelopeVersion = 1u;
    inline constexpr WideStringView kInstanceExt = u".rasset";

    class ContentDatabase;
    class Group;

    // Path + envelope helpers (defined below; declared here for in-class use).
    [[nodiscard]] inline WideString JoinPath(WideStringView a, WideStringView b);
    [[nodiscard]] inline bool EndsWith(WideStringView str, WideStringView suffix);
    inline void WriteEnvelope(IStream& out, const Guid& id, WideStringView typeNs, WideStringView typeName,
                              ISerializable& object);
    inline bool ReadEnvelopeHeader(IStream& in, Guid& outId, WideString& outNs, WideString& outName);

    // =======================================================================
    // Instance — one stored unit: identity + a primary object + data streams.
    // =======================================================================
    class Instance
    {
    public:
        Instance(ContentDatabase& db, Group& group, const Guid& id, WideStringView name,
                 WideStringView typeNamespace, WideStringView typeName)
            : m_db(&db), m_group(&group), m_id(id), m_name(name)
            , m_typeNamespace(typeNamespace), m_typeName(typeName) {}

        [[nodiscard]] const Guid& Id() const noexcept { return m_id; }
        [[nodiscard]] WideStringView Name() const noexcept { return m_name.AsView(); }
        [[nodiscard]] WideStringView TypeNamespace() const noexcept { return m_typeNamespace.AsView(); }
        [[nodiscard]] WideStringView TypeName() const noexcept { return m_typeName.AsView(); }
        [[nodiscard]] Group& OwningGroup() const noexcept { return *m_group; }

        // "group/path/name" (mount-relative, no extension).
        [[nodiscard]] WideString Path() const;

        // Deserializes the primary object (constructing the concrete type from
        // its stored type name). Null if the type isn't registered or on I/O error.
        [[nodiscard]] RefPtr<ISerializable> ReadObject() const;

        // Opens a named data stream for reading, or null if absent.
        [[nodiscard]] UniquePtr<IStream> ReadData(WideStringView streamName) const;

        // --- tooling / write ---
        [[nodiscard]] Status WriteObject(ISerializable& object);
        [[nodiscard]] Status WriteData(WideStringView streamName, Span<const byte> data);

    private:
        [[nodiscard]] WideString EnvelopePath() const;     // "<path>.rasset"
        [[nodiscard]] WideString DataPath(WideStringView streamName) const; // "<path>.<stream>.bin"

        ContentDatabase* m_db;
        Group* m_group;
        Guid m_id;
        WideString m_name;
        WideString m_typeNamespace;
        WideString m_typeName;
    };

    // =======================================================================
    // Group — a folder in the tree: child groups + instances.
    // =======================================================================
    class Group
    {
    public:
        Group(ContentDatabase& db, Group* parent, WideStringView name)
            : m_db(&db), m_parent(parent), m_name(name) {}

        [[nodiscard]] WideStringView Name() const noexcept { return m_name.AsView(); }
        [[nodiscard]] Group* Parent() const noexcept { return m_parent; }

        // Mount-relative folder path ("" at the root, "materials/metal" deeper).
        [[nodiscard]] WideString Path() const;

        [[nodiscard]] Span<Group* const> Groups() const noexcept { return m_groups.AsSpan(); }
        [[nodiscard]] Span<Instance* const> Instances() const noexcept { return m_instances.AsSpan(); }

        [[nodiscard]] Group* GetGroup(WideStringView name) const;
        [[nodiscard]] Instance* GetInstance(WideStringView name) const;

        // --- tooling ---
        // Returns the existing child group of this name, or creates it (no disk
        // write until an instance is committed under it).
        Group* CreateGroup(WideStringView name);
        // Creates a new instance of `primaryType` with a fresh Guid. The on-disk
        // file appears once WriteObject() is called.
        Instance* CreateInstance(WideStringView name, const TypeInfo& primaryType);

        // --- internal (used by the database scanner) ---
        Group* AddChildGroup(WideStringView name);
        Instance* AddInstance(const Guid& id, WideStringView name, WideStringView typeNs, WideStringView typeName);

    private:
        ContentDatabase* m_db;
        Group* m_parent;
        WideString m_name;
        Array<Group*> m_groups;        // owned by the database pool
        Array<Instance*> m_instances;  // owned by the database pool
    };

    // =======================================================================
    // IContentDatabase — the database surface (backends implement it).
    // =======================================================================
    class IContentDatabase
    {
    public:
        virtual ~IContentDatabase() = default;

        [[nodiscard]] virtual Group* RootGroup() = 0;
        [[nodiscard]] virtual Instance* GetInstance(const Guid& id) = 0;
        [[nodiscard]] virtual Instance* GetInstance(WideStringView path) = 0;
        [[nodiscard]] virtual RefPtr<ISerializable> ReadObject(const Guid& id) = 0;
    };

    // =======================================================================
    // ContentDatabase — VFS-backed. Scans the mount on construction; reads and
    // writes through the mount's enumerable/writable capabilities.
    // =======================================================================
    class ContentDatabase final : public IContentDatabase
    {
    public:
        // `mount` must outlive the database and support enumerate + write.
        explicit ContentDatabase(IFileSystem& mount,
                                 SerializableRegistry& serializables = GlobalSerializableRegistry(),
                                 TypeRegistry& types = GlobalTypeRegistry())
            : m_mount(&mount), m_serializables(&serializables), m_types(&types)
        {
            m_root = NewGroup(nullptr, u"");
            Scan(*m_root, u"");
        }

        ~ContentDatabase() override
        {
            for (Instance* instance : m_allInstances) { DefaultAllocator().Delete(instance); }
            for (Group* group : m_allGroups) { DefaultAllocator().Delete(group); }
        }

        ContentDatabase(const ContentDatabase&) = delete;
        ContentDatabase& operator=(const ContentDatabase&) = delete;

        [[nodiscard]] Group* RootGroup() override { return m_root; }

        [[nodiscard]] Instance* GetInstance(const Guid& id) override
        {
            Instance* const* found = m_byGuid.Find(id);
            return (found != nullptr) ? *found : nullptr;
        }

        [[nodiscard]] Instance* GetInstance(WideStringView path) override
        {
            // Split "group/sub/name" -> walk groups, then the instance by name.
            Group* group = m_root;
            usize start = 0;
            for (usize i = 0; i <= path.Size(); ++i)
            {
                const bool atEnd = (i == path.Size());
                if (!atEnd && path[i] != u'/') { continue; }
                const WideStringView part = path.SubStr(start, i - start);
                start = i + 1;
                if (part.IsEmpty()) { continue; }
                if (atEnd) { return group->GetInstance(part); }   // last segment = instance name
                Group* next = group->GetGroup(part);
                if (next == nullptr) { return nullptr; }
                group = next;
            }
            return nullptr;
        }

        [[nodiscard]] RefPtr<ISerializable> ReadObject(const Guid& id) override
        {
            Instance* instance = GetInstance(id);
            return (instance != nullptr) ? instance->ReadObject() : RefPtr<ISerializable>{};
        }

        // --- accessors used by Group/Instance ---
        [[nodiscard]] IFileSystem& Mount() const noexcept { return *m_mount; }
        [[nodiscard]] SerializableRegistry& Serializables() const noexcept { return *m_serializables; }
        [[nodiscard]] TypeRegistry& Types() const noexcept { return *m_types; }
        [[nodiscard]] Random& Rng() noexcept { return m_rng; }

        Group* NewGroup(Group* parent, WideStringView name)
        {
            Group* group = DefaultAllocator().New<Group>(*this, parent, name);
            m_allGroups.PushBack(group);
            return group;
        }

        Instance* NewInstance(Group& group, const Guid& id, WideStringView name,
                              WideStringView typeNs, WideStringView typeName)
        {
            Instance* instance = DefaultAllocator().New<Instance>(*this, group, id, name, typeNs, typeName);
            m_allInstances.PushBack(instance);
            if (!id.IsNil()) { m_byGuid.InsertOrAssign(id, instance); }
            return instance;
        }

    private:
        // Recursively scans `folder` (mount-relative) into `group`.
        void Scan(Group& group, WideStringView folder)
        {
            IEnumerableFileSystem* enumerable = m_mount->AsEnumerable();
            if (enumerable == nullptr) { return; }

            Array<DirEntry> entries;
            if (!enumerable->Enumerate(folder, entries).IsOk()) { return; }

            for (const DirEntry& entry : entries)
            {
                if (entry.isDirectory)
                {
                    Group* child = group.AddChildGroup(entry.name.AsView());
                    Scan(*child, JoinPath(folder, entry.name.AsView()));
                }
                else if (EndsWith(entry.name.AsView(), kInstanceExt))
                {
                    ScanInstance(group, folder, entry.name.AsView());
                }
            }
        }

        void ScanInstance(Group& group, WideStringView folder, WideStringView fileName)
        {
            const WideStringView instanceName = fileName.SubStr(0, fileName.Size() - kInstanceExt.Size());
            UniquePtr<IStream> stream = m_mount->Open(JoinPath(folder, fileName), FileMode::Read);
            if (!stream) { return; }

            Guid id;
            WideString typeNs;
            WideString typeName;
            if (!ReadEnvelopeHeader(*stream, id, typeNs, typeName)) { return; }
            (void)group.AddInstance(id, instanceName, typeNs.AsView(), typeName.AsView());
        }

        IFileSystem* m_mount;
        SerializableRegistry* m_serializables;
        TypeRegistry* m_types;
        Random m_rng;
        Group* m_root = nullptr;
        Array<Group*> m_allGroups;
        Array<Instance*> m_allInstances;
        HashMap<Guid, Instance*> m_byGuid;
    };

    // -----------------------------------------------------------------------
    // Path helpers (mount-relative, forward-slash).
    // -----------------------------------------------------------------------
    [[nodiscard]] inline WideString JoinPath(WideStringView a, WideStringView b)
    {
        if (a.IsEmpty()) { return WideString(b); }
        if (b.IsEmpty()) { return WideString(a); }
        WideString out(a);
        out.PushBack(u'/');
        out.Append(b);
        return out;
    }

    [[nodiscard]] inline bool EndsWith(WideStringView str, WideStringView suffix)
    {
        return str.Size() >= suffix.Size()
            && str.SubStr(str.Size() - suffix.Size(), suffix.Size()) == suffix;
    }

    // -----------------------------------------------------------------------
    // Envelope I/O: [magic][version][guid.high][guid.low][typeNs][typeName][payload]
    // -----------------------------------------------------------------------
    inline void WriteEnvelope(IStream& out, const Guid& id, WideStringView typeNs, WideStringView typeName,
                              ISerializable& object)
    {
        BinarySerializer ar(out, SerializeMode::Write);
        u32 magic = kEnvelopeMagic;
        u32 version = kEnvelopeVersion;
        u64 high = id.high;
        u64 low = id.low;
        WideString ns(typeNs);
        WideString nm(typeName);
        Serialize(ar, magic);
        Serialize(ar, version);
        Serialize(ar, high);
        Serialize(ar, low);
        Serialize(ar, ns);
        Serialize(ar, nm);
        object.Serialize(ar);
    }

    // Reads just the header fields (for scanning). Leaves the stream positioned
    // at the payload. Returns false on bad magic / short read.
    inline bool ReadEnvelopeHeader(IStream& in, Guid& outId, WideString& outNs, WideString& outName)
    {
        BinarySerializer ar(in, SerializeMode::Read);
        u32 magic = 0;
        u32 version = 0;
        u64 high = 0;
        u64 low = 0;
        Serialize(ar, magic);
        Serialize(ar, version);
        Serialize(ar, high);
        Serialize(ar, low);
        Serialize(ar, outNs);
        Serialize(ar, outName);
        if (!ar.IsOk() || magic != kEnvelopeMagic) { return false; }
        outId = Guid{ high, low };
        return true;
    }

    // -----------------------------------------------------------------------
    // Group method definitions.
    // -----------------------------------------------------------------------
    inline WideString Group::Path() const
    {
        if (m_parent == nullptr) { return WideString(); }    // root
        return JoinPath(m_parent->Path().AsView(), m_name.AsView());
    }

    inline Group* Group::GetGroup(WideStringView name) const
    {
        for (Group* group : m_groups)
        {
            if (group->Name() == name) { return group; }
        }
        return nullptr;
    }

    inline Instance* Group::GetInstance(WideStringView name) const
    {
        for (Instance* instance : m_instances)
        {
            if (instance->Name() == name) { return instance; }
        }
        return nullptr;
    }

    inline Group* Group::AddChildGroup(WideStringView name)
    {
        Group* child = m_db->NewGroup(this, name);
        m_groups.PushBack(child);
        return child;
    }

    inline Instance* Group::AddInstance(const Guid& id, WideStringView name, WideStringView typeNs, WideStringView typeName)
    {
        Instance* instance = m_db->NewInstance(*this, id, name, typeNs, typeName);
        m_instances.PushBack(instance);
        return instance;
    }

    inline Group* Group::CreateGroup(WideStringView name)
    {
        Group* existing = GetGroup(name);
        return (existing != nullptr) ? existing : AddChildGroup(name);
    }

    inline Instance* Group::CreateInstance(WideStringView name, const TypeInfo& primaryType)
    {
        if (Instance* existing = GetInstance(name)) { return existing; }
        const Guid id = Guid::Generate(m_db->Rng());
        // TypeInfo names are narrow ASCII; the envelope stores them wide.
        const WideString ns = ToWide(StringView(reinterpret_cast<const utf8char*>(primaryType.namespaceName)));
        const WideString nm = ToWide(StringView(reinterpret_cast<const utf8char*>(primaryType.name)));
        return AddInstance(id, name, ns.AsView(), nm.AsView());
    }

    // -----------------------------------------------------------------------
    // Instance method definitions.
    // -----------------------------------------------------------------------
    inline WideString Instance::Path() const
    {
        return JoinPath(m_group->Path().AsView(), m_name.AsView());
    }

    inline WideString Instance::EnvelopePath() const
    {
        WideString path = Path();
        path.Append(kInstanceExt);
        return path;
    }

    inline WideString Instance::DataPath(WideStringView streamName) const
    {
        WideString path = Path();
        path.PushBack(u'.');
        path.Append(streamName);
        path.Append(u".bin");
        return path;
    }

    inline RefPtr<ISerializable> Instance::ReadObject() const
    {
        UniquePtr<IStream> stream = m_db->Mount().Open(EnvelopePath().AsView(), FileMode::Read);
        if (!stream) { return RefPtr<ISerializable>{}; }

        WideString ns;
        WideString name;
        // Read the header off this serializer, then continue into the payload.
        BinarySerializer ar(*stream, SerializeMode::Read);
        u32 magic = 0;
        u32 version = 0;
        u64 high = 0;
        u64 low = 0;
        Serialize(ar, magic);
        Serialize(ar, version);
        Serialize(ar, high);
        Serialize(ar, low);
        Serialize(ar, ns);
        Serialize(ar, name);
        if (!ar.IsOk() || magic != kEnvelopeMagic) { return RefPtr<ISerializable>{}; }

        const String nsU8 = ToUTF8(ns.AsView());
        const String nameU8 = ToUTF8(name.AsView());
        const TypeInfo* type = m_db->Types().FindByName(
            reinterpret_cast<const char*>(nsU8.CStr()),
            reinterpret_cast<const char*>(nameU8.CStr()));
        if (type == nullptr) { return RefPtr<ISerializable>{}; }

        RefPtr<ISerializable> object = m_db->Serializables().Create(type->id);
        if (object.Get() == nullptr) { return RefPtr<ISerializable>{}; }

        object->Serialize(ar);
        return ar.IsOk() ? object : RefPtr<ISerializable>{};
    }

    inline UniquePtr<IStream> Instance::ReadData(WideStringView streamName) const
    {
        return m_db->Mount().Open(DataPath(streamName).AsView(), FileMode::Read);
    }

    inline Status Instance::WriteObject(ISerializable& object)
    {
        IWritableFileSystem* writable = m_db->Mount().AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        MemoryStream buffer;
        WriteEnvelope(buffer, m_id, m_typeNamespace.AsView(), m_typeName.AsView(), object);
        return writable->Save(EnvelopePath().AsView(), buffer.Bytes());
    }

    inline Status Instance::WriteData(WideStringView streamName, Span<const byte> data)
    {
        IWritableFileSystem* writable = m_db->Mount().AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }
        return writable->Save(DataPath(streamName).AsView(), data);
    }
}
