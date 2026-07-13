// Draconic::Content - the `draconic.content` module.
//
// A content database: a hierarchical store of serializable objects, addressed by
// Guid (stable) or by path. A Group is a folder; an Instance is one stored unit
// (a Guid + a primary ISerializable object + named data streams for heavy
// blobs). Backed by a VFS mount (Group = directory, Instance = a file with a
// configurable extension, data streams = sidecar files). Identity is decoupled
// from byte access: the database owns the Guid<->location structure; the VFS
// owns the bytes.
//
// The serialization format is pluggable: the database receives a
// SerializerFactory that creates a Serializer for a given stream + mode. The
// factory hides format-specific construction (binary vs XML vs anything else).
// The database never imports a concrete serializer module.

module;
#include "Core/Prelude.h"

export module draconic.content;

import draconic.core;
import draconic.vfs;

using namespace draconic::core;
using namespace draconic::vfs;

export namespace draconic::content
{
    class ContentDatabase;
    class Group;

    // Path helpers (defined below; declared here for in-class use).
    [[nodiscard]] inline String JoinPath(StringView a, StringView b);
    [[nodiscard]] inline bool EndsWith(StringView str, StringView suffix);

    // =======================================================================
    // Instance - one stored unit: identity + a primary object + data streams.
    // =======================================================================
    class Instance
    {
    public:
        Instance(ContentDatabase& db, Group& group, const Guid& id, StringView name,
                 StringView typeNamespace, StringView typeName)
            : m_db(&db), m_group(&group), m_id(id), m_name(name)
            , m_typeNamespace(typeNamespace), m_typeName(typeName) {}

        [[nodiscard]] const Guid& Id() const noexcept { return m_id; }
        [[nodiscard]] StringView Name() const noexcept { return m_name.AsView(); }
        [[nodiscard]] StringView TypeNamespace() const noexcept { return m_typeNamespace.AsView(); }
        [[nodiscard]] StringView TypeName() const noexcept { return m_typeName.AsView(); }
        [[nodiscard]] Group& OwningGroup() const noexcept { return *m_group; }

        // "group/path/name" (mount-relative, no extension).
        [[nodiscard]] String Path() const;

        // Deserializes the primary object (constructing the concrete type from
        // its stored type name). Null if the type isn't registered or on I/O error.
        [[nodiscard]] RefPtr<ISerializable> ReadObject() const;

        // Opens a named data stream for reading, or null if absent.
        [[nodiscard]] UniquePtr<IStream> ReadData(StringView streamName) const;
        // The raw on-disk envelope (identity header + primary object + stream directory) -
        // the cook driver hashes it as the asset's settings/content fingerprint.
        [[nodiscard]] UniquePtr<IStream> OpenEnvelope() const;

        // --- tooling / write ---
        [[nodiscard]] Status WriteObject(ISerializable& object);
        [[nodiscard]] Status WriteData(StringView streamName, Span<const byte> data);

    private:
        friend class ContentDatabase;   // storage layout (envelope/sidecar paths) for delete
        [[nodiscard]] String EnvelopePath() const;     // "<path>.<ext>"
        [[nodiscard]] String DataPath(StringView streamName) const; // "<path>.<stream>.bin"

        ContentDatabase* m_db;
        Group* m_group;
        Guid m_id;
        String m_name;
        String m_typeNamespace;
        String m_typeName;
    };

    // =======================================================================
    // Group - a folder in the tree: child groups + instances.
    // =======================================================================
    class Group
    {
    public:
        Group(ContentDatabase& db, Group* parent, StringView name)
            : m_db(&db), m_parent(parent), m_name(name) {}

        [[nodiscard]] StringView Name() const noexcept { return m_name.AsView(); }
        [[nodiscard]] Group* Parent() const noexcept { return m_parent; }

        // Mount-relative folder path ("" at the root, "materials/metal" deeper).
        [[nodiscard]] String Path() const;

        [[nodiscard]] Span<Group* const> Groups() const noexcept { return m_groups.AsSpan(); }
        [[nodiscard]] Span<Instance* const> Instances() const noexcept { return m_instances.AsSpan(); }

        [[nodiscard]] Group* GetGroup(StringView name) const;
        [[nodiscard]] Instance* GetInstance(StringView name) const;

        // --- tooling ---
        // Returns the existing child group of this name, or creates it (no disk
        // write until an instance is committed under it).
        Group* CreateGroup(StringView name);
        // Creates a new instance of `primaryType` with a fresh Guid. The on-disk
        // file appears once WriteObject() is called.
        Instance* CreateInstance(StringView name, const TypeInfo& primaryType);
        // Same, but with a caller-chosen Guid (the cook driver: product guid = source guid).
        // Returns the existing instance when the name is already taken.
        Instance* CreateInstanceWithId(const Guid& id, StringView name, const TypeInfo& primaryType);

        // --- internal (used by the database scanner) ---
        Group* AddChildGroup(StringView name);
        Instance* AddInstance(const Guid& id, StringView name, StringView typeNs, StringView typeName);
        void RemoveInstance(Instance* instance);   // unlinks from this group (DB owns destruction)

    private:
        friend class ContentDatabase;   // rename rewrites m_name after moving the directory
        ContentDatabase* m_db;
        Group* m_parent;
        String m_name;
        Array<Group*> m_groups;        // owned by the database pool
        Array<Instance*> m_instances;  // owned by the database pool
    };

    // =======================================================================
    // IContentDatabase - the database surface (backends implement it).
    // =======================================================================
    class IContentDatabase
    {
    public:
        virtual ~IContentDatabase() = default;

        [[nodiscard]] virtual Group* RootGroup() = 0;
        [[nodiscard]] virtual Instance* GetInstance(const Guid& id) = 0;
        [[nodiscard]] virtual Instance* GetInstance(StringView path) = 0;
        [[nodiscard]] virtual RefPtr<ISerializable> ReadObject(const Guid& id) = 0;
    };

    // =======================================================================
    // ContentDatabase - VFS-backed. Scans the mount on construction; reads and
    // writes through the mount's enumerable/writable capabilities. The
    // serialization format is determined by the SerializerFactory provided by
    // the caller.
    // =======================================================================
    class ContentDatabase final : public IContentDatabase
    {
    public:
        // `mount` must outlive the database and support enumerate + write.
        explicit ContentDatabase(IFileSystem& mount,
                                 SerializerFactory factory,
                                 StringView fileExtension,
                                 SerializableRegistry& serializables = GlobalSerializableRegistry(),
                                 TypeRegistry& types = GlobalTypeRegistry())
            : m_mount(&mount), m_factory(static_cast<SerializerFactory&&>(factory))
            , m_extension(fileExtension)
            , m_serializables(&serializables), m_types(&types)
        {
            m_root = NewGroup(nullptr, u8"");
            Scan(*m_root, u8"");
        }

        ~ContentDatabase() override
        {
            for (Instance* instance : m_allInstances) { DefaultAllocator().Delete(instance); }
            for (Group* group : m_allGroups) { DefaultAllocator().Delete(group); }
        }

        ContentDatabase(const ContentDatabase&) = delete;
        ContentDatabase& operator=(const ContentDatabase&) = delete;

        [[nodiscard]] Group* RootGroup() override { return m_root; }

        // Delete an instance: its envelope + every data-stream sidecar are removed from the
        // mount, and it is unregistered from the group tree and the GUID index. (Cook orphan
        // sweep + browser Delete.) NotFound when the id is unknown.
        Status DeleteInstance(const Guid& id);

        // Clone an instance under `newName` in the SAME group, with a fresh Guid: the primary
        // object round-trips through its registered type (so the copy is deep and re-keyed) and
        // every data-stream sidecar is byte-copied. Null when the id is unknown, the name is
        // taken, or the primary type isn't registered. (Browser Duplicate.)
        Instance* CloneInstance(const Guid& id, StringView newName);

        // Rename an instance IN PLACE (same group, same guid): moves the envelope and every
        // data-stream sidecar on disk (the name IS the filename - envelopes don't store it).
        // Guid-based references (scene refs, cook records) are untouched by design.
        // AlreadyExists when the name is taken; NotSupported on read-only mounts.
        Status RenameInstance(const Guid& id, StringView newName);

        // Rename a group (directory move; child paths derive dynamically, so descendants
        // need no fixup). NotSupported for the root or read-only mounts.
        Status RenameGroup(Group& group, StringView newName);

        // Delete a group and EVERYTHING under it: every instance (envelope + sidecars, via
        // DeleteInstance) and every child group, bottom-up, then the now-empty directories -
        // a rescan must not resurrect ghost groups. The Group object is destroyed; the caller's
        // pointer is dangling after success. NotSupported for the root or read-only mounts.
        Status DeleteGroup(Group& group);

        [[nodiscard]] Instance* GetInstance(const Guid& id) override
        {
            Instance* const* found = m_byGuid.Find(id);
            return (found != nullptr) ? *found : nullptr;
        }

        [[nodiscard]] Instance* GetInstance(StringView path) override
        {
            // Split "group/sub/name" -> walk groups, then the instance by name.
            Group* group = m_root;
            usize start = 0;
            for (usize i = 0; i <= path.Size(); ++i)
            {
                const bool atEnd = (i == path.Size());
                if (!atEnd && path[i] != utf8char('/')) { continue; }
                const StringView part = path.SubStr(start, i - start);
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
        [[nodiscard]] StringView Extension() const noexcept { return m_extension.AsView(); }

        [[nodiscard]] UniquePtr<SerializerContext> CreateSerializer(IStream& stream, SerializeMode mode) const
        {
            return m_factory(stream, mode);
        }

        Group* NewGroup(Group* parent, StringView name)
        {
            Group* group = DefaultAllocator().New<Group>(*this, parent, name);
            m_allGroups.PushBack(group);
            return group;
        }

        Instance* NewInstance(Group& group, const Guid& id, StringView name,
                              StringView typeNs, StringView typeName)
        {
            Instance* instance = DefaultAllocator().New<Instance>(*this, group, id, name, typeNs, typeName);
            m_allInstances.PushBack(instance);
            if (!id.IsNil()) { m_byGuid.InsertOrAssign(id, instance); }
            return instance;
        }

    private:
        // Recursively scans `folder` (mount-relative) into `group`.
        void Scan(Group& group, StringView folder)
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
                else if (EndsWith(entry.name.AsView(), m_extension.AsView()))
                {
                    ScanInstance(group, folder, entry.name.AsView());
                }
            }
        }

        void ScanInstance(Group& group, StringView folder, StringView fileName)
        {
            const StringView instanceName = fileName.SubStr(0, fileName.Size() - m_extension.Size());
            UniquePtr<IStream> stream = m_mount->Open(JoinPath(folder, fileName), FileMode::Read);
            if (!stream) { return; }

            UniquePtr<SerializerContext> ctx = m_factory(*stream, SerializeMode::Read);
            if (!ctx || ctx->serializer == nullptr) { return; }
            Serializer& ar = *ctx->serializer;

            Guid id;
            String typeNs;
            String typeName;
            ar.Key("guid");     ar.GuidValue(id);
            ar.Key("typeNamespace");   ar.Text(typeNs);
            ar.Key("typeName"); ar.Text(typeName);
            if (!ar.IsOk()) { return; }

            (void)group.AddInstance(id, instanceName, typeNs.AsView(), typeName.AsView());
        }

        IFileSystem* m_mount;
        SerializerFactory m_factory;
        String m_extension;
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
    [[nodiscard]] inline String JoinPath(StringView a, StringView b)
    {
        if (a.IsEmpty()) { return String(b); }
        if (b.IsEmpty()) { return String(a); }
        String out(a);
        out.PushBack(utf8char('/'));
        out.Append(b);
        return out;
    }

    [[nodiscard]] inline bool EndsWith(StringView str, StringView suffix)
    {
        return str.Size() >= suffix.Size()
            && str.SubStr(str.Size() - suffix.Size(), suffix.Size()) == suffix;
    }

    // -----------------------------------------------------------------------
    // Group method definitions.
    // -----------------------------------------------------------------------
    inline String Group::Path() const
    {
        if (m_parent == nullptr) { return String(); }    // root
        return JoinPath(m_parent->Path().AsView(), m_name.AsView());
    }

    inline Group* Group::GetGroup(StringView name) const
    {
        for (Group* group : m_groups)
        {
            if (group->Name() == name) { return group; }
        }
        return nullptr;
    }

    inline Instance* Group::GetInstance(StringView name) const
    {
        for (Instance* instance : m_instances)
        {
            if (instance->Name() == name) { return instance; }
        }
        return nullptr;
    }

    inline Group* Group::AddChildGroup(StringView name)
    {
        Group* child = m_db->NewGroup(this, name);
        m_groups.PushBack(child);
        return child;
    }

    inline Instance* Group::AddInstance(const Guid& id, StringView name, StringView typeNs, StringView typeName)
    {
        Instance* instance = m_db->NewInstance(*this, id, name, typeNs, typeName);
        m_instances.PushBack(instance);
        return instance;
    }

    inline void Group::RemoveInstance(Instance* instance)
    {
        for (usize i = 0; i < m_instances.Size(); ++i)
        {
            if (m_instances[i] == instance)
            {
                m_instances.RemoveAt(i);
                return;
            }
        }
    }

    inline Group* Group::CreateGroup(StringView name)
    {
        Group* existing = GetGroup(name);
        return (existing != nullptr) ? existing : AddChildGroup(name);
    }

    inline Instance* Group::CreateInstance(StringView name, const TypeInfo& primaryType)
    {
        if (Instance* existing = GetInstance(name)) { return existing; }
        // Mint a GUID that isn't already in use. The RNG is deterministic and Scan() (load-from-disk)
        // does NOT advance it past the instances it loads - so a fresh instance added to a scanned DB
        // would otherwise reproduce the FIRST-cooked instance's GUID and alias it (e.g. a runtime-cooked
        // texture colliding with a model's first texture). Re-roll until the id is free.
        Guid id = Guid::Generate(m_db->Rng());
        while (m_db->GetInstance(id) != nullptr) { id = Guid::Generate(m_db->Rng()); }
        // TypeInfo names are narrow ASCII; wrap in StringView.
        const StringView ns(reinterpret_cast<const utf8char*>(primaryType.namespaceName));
        const StringView nm(reinterpret_cast<const utf8char*>(primaryType.name));
        return AddInstance(id, name, ns, nm);
    }

    inline Instance* Group::CreateInstanceWithId(const Guid& id, StringView name, const TypeInfo& primaryType)
    {
        if (Instance* existing = GetInstance(name)) { return existing; }
        const StringView ns(reinterpret_cast<const utf8char*>(primaryType.namespaceName));
        const StringView nm(reinterpret_cast<const utf8char*>(primaryType.name));
        return AddInstance(id, name, ns, nm);
    }

    // -----------------------------------------------------------------------
    // Instance method definitions.
    // -----------------------------------------------------------------------
    inline String Instance::Path() const
    {
        return JoinPath(m_group->Path().AsView(), m_name.AsView());
    }

    inline String Instance::EnvelopePath() const
    {
        String path = Path();
        path.Append(m_db->Extension());
        return path;
    }

    inline String Instance::DataPath(StringView streamName) const
    {
        String path = Path();
        path.PushBack(utf8char('.'));
        path.Append(streamName);
        path.Append(u8".bin");
        return path;
    }

    inline RefPtr<ISerializable> Instance::ReadObject() const
    {
        UniquePtr<IStream> stream = m_db->Mount().Open(EnvelopePath().AsView(), FileMode::Read);
        if (!stream) { return RefPtr<ISerializable>{}; }

        UniquePtr<SerializerContext> ctx = m_db->CreateSerializer(*stream, SerializeMode::Read);
        if (!ctx || ctx->serializer == nullptr) { return RefPtr<ISerializable>{}; }
        Serializer& ar = *ctx->serializer;

        // Read header.
        Guid id;
        String ns;
        String name;
        ar.Key("guid");     ar.GuidValue(id);
        ar.Key("typeNamespace");   ar.Text(ns);
        ar.Key("typeName"); ar.Text(name);
        if (!ar.IsOk()) { return RefPtr<ISerializable>{}; }

        // Resolve the type and construct the object.
        const TypeInfo* type = m_db->Types().FindByName(
            reinterpret_cast<const char*>(ns.CStr()),
            reinterpret_cast<const char*>(name.CStr()));
        if (type == nullptr) { return RefPtr<ISerializable>{}; }

        RefPtr<ISerializable> object = m_db->Serializables().Create(type->id);
        if (object.Get() == nullptr) { return RefPtr<ISerializable>{}; }

        // Deserialize the payload under the STORED data-version scope (migration branches in
        // Serialize see the version the envelope was written with).
        BeginVersionedPayload(ar, *type);
        ar.Key("payload");  ar.BeginObject();
        object->Serialize(ar);
        ar.EndObject();
        EndVersionedPayload(ar);
        return ar.IsOk() ? object : RefPtr<ISerializable>{};
    }

    inline UniquePtr<IStream> Instance::ReadData(StringView streamName) const
    {
        return m_db->Mount().Open(DataPath(streamName).AsView(), FileMode::Read);
    }

    inline Status Instance::WriteObject(ISerializable& object)
    {
        IWritableFileSystem* writable = m_db->Mount().AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        MemoryStream buffer;
        UniquePtr<SerializerContext> ctx = m_db->CreateSerializer(buffer, SerializeMode::Write);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        Serializer& ar = *ctx->serializer;

        // Write header + payload (payload wrapped in the object's data-version scope, so
        // Serialize bodies can branch on ar.Version() for migration).
        String ns(m_typeNamespace);
        String nm(m_typeName);
        ar.Key("guid");     ar.GuidValue(const_cast<Guid&>(m_id));
        ar.Key("typeNamespace");   ar.Text(ns);
        ar.Key("typeName"); ar.Text(nm);
        BeginVersionedPayload(ar, *object.GetType());
        ar.Key("payload");  ar.BeginObject();
        object.Serialize(ar);
        ar.EndObject();
        EndVersionedPayload(ar);

        // Let the context flush (e.g., XML writes its text output here).
        ctx->Flush(buffer);

        return writable->Save(EnvelopePath().AsView(), buffer.Bytes());
    }

    inline Status Instance::WriteData(StringView streamName, Span<const byte> data)
    {
        IWritableFileSystem* writable = m_db->Mount().AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }
        return writable->Save(DataPath(streamName).AsView(), data);
    }

    inline UniquePtr<IStream> Instance::OpenEnvelope() const
    {
        return m_db->Mount().Open(EnvelopePath().AsView(), FileMode::Read);
    }

    inline Instance* ContentDatabase::CloneInstance(const Guid& id, StringView newName)
    {
        Instance* src = GetInstance(id);
        if (src == nullptr) { return nullptr; }
        Group& group = src->OwningGroup();
        if (group.GetInstance(newName) != nullptr) { return nullptr; }

        // The primary object must round-trip (re-serialized under the clone's identity - a raw
        // envelope byte-copy would carry the SOURCE guid).
        RefPtr<ISerializable> object = src->ReadObject();
        if (object.Get() == nullptr) { return nullptr; }

        Guid cloneId = Guid::Generate(Rng());
        while (GetInstance(cloneId) != nullptr) { cloneId = Guid::Generate(Rng()); }
        Instance* copy = group.AddInstance(cloneId, newName, src->TypeNamespace(), src->TypeName());
        if (copy == nullptr) { return nullptr; }
        if (!copy->WriteObject(*object).IsOk()) { return nullptr; }

        // Sidecar streams keep no directory - enumerate "<srcName>.<stream>.bin" siblings (the
        // same prefix scan DeleteInstance uses) and byte-copy each under the clone's name.
        if (IEnumerableFileSystem* enumerable = m_mount->AsEnumerable())
        {
            const String folder = group.Path();
            String prefix(src->Name());
            prefix.PushBack(utf8char('.'));
            Array<DirEntry> entries;
            if (enumerable->Enumerate(folder.AsView(), entries).IsOk())
            {
                for (const DirEntry& entry : entries)
                {
                    if (entry.isDirectory || entry.name.Size() <= prefix.Size()) { continue; }
                    if (entry.name.AsView().SubStr(0, prefix.Size()) != prefix.AsView()) { continue; }
                    if (!EndsWith(entry.name.AsView(), u8".bin")) { continue; }
                    // "<src>.<stream>.bin" -> stream name between prefix and ".bin".
                    const StringView fileName = entry.name.AsView();
                    const StringView stream = fileName.SubStr(prefix.Size(), fileName.Size() - prefix.Size() - 4);
                    if (stream.IsEmpty()) { continue; }
                    if (UniquePtr<IStream> data = src->ReadData(stream))
                    {
                        Array<byte> bytes;
                        bytes.Resize(static_cast<usize>(data->Size()));
                        if (data->Read(bytes.Data(), bytes.Size()) == bytes.Size())
                        {
                            (void)copy->WriteData(stream, Span<const byte>{ bytes.Data(), bytes.Size() });
                        }
                    }
                }
            }
        }
        return copy;
    }

    inline Status ContentDatabase::RenameInstance(const Guid& id, StringView newName)
    {
        Instance* instance = GetInstance(id);
        if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
        if (newName.IsEmpty() || newName == instance->Name()) { return Status{ ErrorCode::InvalidArgument }; }
        if (instance->OwningGroup().GetInstance(newName) != nullptr) { return Status{ ErrorCode::AlreadyExists }; }
        IWritableFileSystem* writable = m_mount->AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        const String folder = instance->OwningGroup().Path();
        const String oldEnvelope = instance->EnvelopePath();

        // Sidecars first (prefix scan, like delete): "<old>.<stream>.bin" -> "<new>.<stream>.bin".
        if (IEnumerableFileSystem* enumerable = m_mount->AsEnumerable())
        {
            String prefix(instance->Name());
            prefix.PushBack(utf8char('.'));
            Array<DirEntry> entries;
            if (enumerable->Enumerate(folder.AsView(), entries).IsOk())
            {
                for (const DirEntry& entry : entries)
                {
                    if (entry.isDirectory || entry.name.Size() <= prefix.Size()) { continue; }
                    if (entry.name.AsView().SubStr(0, prefix.Size()) != prefix.AsView()) { continue; }
                    if (!EndsWith(entry.name.AsView(), u8".bin")) { continue; }
                    String renamed(newName);
                    renamed.Append(entry.name.AsView().SubStr(instance->Name().Size(),
                        entry.name.Size() - instance->Name().Size()));
                    (void)writable->Move(JoinPath(folder.AsView(), entry.name.AsView()).AsView(),
                                         JoinPath(folder.AsView(), renamed.AsView()).AsView());
                }
            }
        }

        // The envelope may not exist yet (instance created, never written) - that's fine.
        instance->m_name = String(newName);
        if (m_mount->Exists(oldEnvelope.AsView()))
        {
            const Status moved = writable->Move(oldEnvelope.AsView(), instance->EnvelopePath().AsView());
            if (!moved.IsOk()) { return moved; }
        }
        return Status{};
    }

    inline Status ContentDatabase::RenameGroup(Group& group, StringView newName)
    {
        if (group.Parent() == nullptr) { return Status{ ErrorCode::NotSupported }; }   // the root
        if (newName.IsEmpty() || newName == group.Name()) { return Status{ ErrorCode::InvalidArgument }; }
        if (group.Parent()->GetGroup(newName) != nullptr) { return Status{ ErrorCode::AlreadyExists }; }
        IWritableFileSystem* writable = m_mount->AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        const String oldPath = group.Path();
        group.m_name = String(newName);
        // The directory may not exist yet (group created, nothing written under it).
        if (!oldPath.IsEmpty() && m_mount->Exists(oldPath.AsView()))
        {
            const Status moved = writable->Move(oldPath.AsView(), group.Path().AsView());
            if (!moved.IsOk()) { return moved; }
        }
        return Status{};
    }

    inline Status ContentDatabase::DeleteInstance(const Guid& id)
    {
        Instance* instance = GetInstance(id);
        if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
        IWritableFileSystem* writable = m_mount->AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        // Envelope + every "<name>.<stream>.bin" sidecar (streams keep no directory, so the
        // group folder is enumerated for siblings with the instance's file prefix).
        (void)writable->Delete(instance->EnvelopePath().AsView());
        if (IEnumerableFileSystem* enumerable = m_mount->AsEnumerable())
        {
            const String folder = instance->OwningGroup().Path();
            String prefix(instance->Name());
            prefix.PushBack(utf8char('.'));
            Array<DirEntry> entries;
            if (enumerable->Enumerate(folder.AsView(), entries).IsOk())
            {
                for (const DirEntry& entry : entries)
                {
                    if (entry.isDirectory || entry.name.Size() <= prefix.Size()) { continue; }
                    if (entry.name.AsView().SubStr(0, prefix.Size()) != prefix.AsView()) { continue; }
                    if (!EndsWith(entry.name.AsView(), u8".bin")) { continue; }
                    (void)writable->Delete(JoinPath(folder.AsView(), entry.name.AsView()).AsView());
                }
            }
        }

        m_byGuid.Remove(id);
        instance->OwningGroup().RemoveInstance(instance);
        for (usize i = 0; i < m_allInstances.Size(); ++i)
        {
            if (m_allInstances[i] == instance)
            {
                m_allInstances.RemoveAtSwap(i);
                break;
            }
        }
        DefaultAllocator().Delete(instance);
        return Status{};
    }

    inline Status ContentDatabase::DeleteGroup(Group& group)
    {
        if (group.Parent() == nullptr) { return Status{ ErrorCode::NotSupported }; }   // the root
        IWritableFileSystem* writable = m_mount->AsWritable();
        if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

        // Instances first (copy the id list - DeleteInstance unlinks from m_instances)...
        Array<Guid> ids;
        for (Instance* instance : group.Instances()) { ids.PushBack(instance->Id()); }
        for (const Guid& id : ids) { (void)DeleteInstance(id); }
        // ...then child groups, bottom-up (copy - the recursion unlinks from m_groups).
        Array<Group*> children;
        for (Group* child : group.Groups()) { children.PushBack(child); }
        for (Group* child : children) { (void)DeleteGroup(*child); }

        // The directory may never have materialized (group created, nothing written).
        const String path = group.Path();
        if (!path.IsEmpty() && m_mount->Exists(path.AsView()))
        {
            const Status removed = writable->DeleteDirectory(path.AsView());
            if (!removed.IsOk()) { return removed; }
        }

        // Unregister from the parent and the pool, then destroy.
        Group* parent = group.Parent();
        for (usize i = 0; i < parent->m_groups.Size(); ++i)
        {
            if (parent->m_groups[i] == &group) { parent->m_groups.RemoveAt(i); break; }
        }
        for (usize i = 0; i < m_allGroups.Size(); ++i)
        {
            if (m_allGroups[i] == &group) { m_allGroups.RemoveAtSwap(i); break; }
        }
        DefaultAllocator().Delete(&group);
        return Status{};
    }
}
