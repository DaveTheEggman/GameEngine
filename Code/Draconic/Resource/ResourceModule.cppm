// Draconic::Resource - the `draconic.resource` module.
//
// The resource manager: turns content-database *source* objects (ISerializable,
// full editor fidelity) into runtime *products* (lean Objects) via factories,
// hands them out behind replaceable handles, and caches/reloads them. This is
// the source->product split: the editor authors a `…Resource` in the content
// db; a factory builds the runtime product the game actually uses. Editor-only
// data (node positions, comments) lives on the source and never reaches the
// product.
//
// Sits at the top of the asset stack: Core/VFS -> content -> resource.

module;
#include "Core/Prelude.h"

export module draconic.resource;

import draconic.core;
import draconic.content;

using namespace draconic::core;

export namespace draconic::resource
{
    // Typed Guid naming a resource that binds to product type T.
    template <typename T>
    struct ResourceId
    {
        Guid id;

        ResourceId() = default;
        explicit ResourceId(const Guid& guid) noexcept : id(guid) {}
        [[nodiscard]] bool IsNull() const noexcept { return id.IsNil(); }
    };

    // =======================================================================
    // ResourceHandle - a shared, replaceable slot holding one runtime product.
    // Proxies hold the handle (not the product), so a Reload that Replace()s the
    // product is seen by every holder. Remembers its product type so the manager
    // can rebuild it without being told the type again.
    // =======================================================================
    class ResourceHandle final : public RefCounted
    {
    public:
        [[nodiscard]] Object* Get() const noexcept { return m_product.Get(); }
        void Replace(RefPtr<Object> product) noexcept { m_product = Move(product); }
        void Flush() noexcept { m_product = nullptr; }

        [[nodiscard]] TypeId ProductTypeId() const noexcept { return m_productTypeId; }
        void SetProductTypeId(TypeId id) noexcept { m_productTypeId = id; }

    private:
        RefPtr<Object> m_product;
        TypeId m_productTypeId{};
    };

    // =======================================================================
    // Proxy<T> - typed accessor over a ResourceHandle. Cheap to copy/store; it
    // follows the handle, so it always sees the current product.
    // =======================================================================
    template <typename T>
    class Proxy
    {
    public:
        Proxy() = default;
        explicit Proxy(RefPtr<ResourceHandle> handle) noexcept : m_handle(Move(handle)) {}

        [[nodiscard]] T* Get() const noexcept
        {
            return (m_handle.Get() != nullptr) ? Cast<T>(m_handle->Get()) : nullptr;
        }
        [[nodiscard]] T* operator->() const noexcept { return Get(); }
        [[nodiscard]] T& operator*() const noexcept { return *Get(); }
        [[nodiscard]] explicit operator bool() const noexcept { return Get() != nullptr; }

        [[nodiscard]] ResourceHandle* Handle() const noexcept { return m_handle.Get(); }

    private:
        RefPtr<ResourceHandle> m_handle;
    };

    // =======================================================================
    // Ref<T> - the SERIALIZABLE resource reference components hold (the asset
    // pipeline's §8 layer). Identity is a Guid (written by Serialize); at runtime
    // Bind() attaches a Proxy so hot reload's handle Replace() is visible to every
    // holder. Code-created resources (samples, procedural) assign a RefPtr<T>
    // directly - the direct object wins over the proxy and is never serialized.
    // =======================================================================
    class ResourceManager;   // forward - Ref::Bind resolves through it

    template <typename T>
    class Ref
    {
    public:
        Guid id;   // serialized identity (nil = unset / procedural-only)

        Ref() = default;
        Ref(const RefPtr<T>& object) : m_direct(object) {}                     // implicit: `c.mesh = meshPtr`
        Ref(T* object) : m_direct(RefPtr<T>(object)) {}                        // implicit: raw runtime objects
        Ref& operator=(const RefPtr<T>& object) { m_direct = object; return *this; }
        Ref& operator=(T* object) { m_direct = RefPtr<T>(object); return *this; }

        [[nodiscard]] T* Get() const noexcept
        {
            return (m_direct.Get() != nullptr) ? m_direct.Get() : m_proxy.Get();
        }
        [[nodiscard]] T* operator->() const noexcept { return Get(); }
        [[nodiscard]] explicit operator bool() const noexcept { return Get() != nullptr; }

        void SetDirect(RefPtr<T> object) noexcept { m_direct = Move(object); }
        void SetId(const Guid& guid) noexcept { id = guid; }
        /// Adopt an already-bound proxy (runtime code that bound by hand): follows reloads;
        /// clears any direct override so the proxy is what Get() sees.
        void SetProxy(Proxy<T> proxy) noexcept { m_proxy = Move(proxy); m_direct = nullptr; }

        /// Re-point at `id` (editor pickers): drops the direct override AND the previous
        /// proxy, then binds the new id (nil = cleared reference).
        void Rebind(ResourceManager* manager)
        {
            m_direct = nullptr;
            m_proxy = Proxy<T>{};
            if (manager != nullptr && !id.IsNil()) { Bind(*manager); }
        }
        [[nodiscard]] bool IsBound() const noexcept { return m_proxy.Handle() != nullptr; }

        /// Drop any runtime binding (proxy AND direct override). Deserialization calls this
        /// when the incoming identity REPLACES a different one - the old binding must not
        /// keep rendering the previous resource.
        void ClearBinding() noexcept { m_proxy = Proxy<T>{}; m_direct = nullptr; }

        // Attach the runtime proxy for `id` (defined after ResourceManager below).
        void Bind(ResourceManager& manager);

        [[nodiscard]] const Proxy<T>& GetProxy() const noexcept { return m_proxy; }

    private:
        Proxy<T> m_proxy;      // guid-backed binding (runtime only)
        RefPtr<T> m_direct;    // procedural override (runtime only)
    };

    // Serialization: identity only (found by ADL from component Serialize bodies). On READ,
    // blobs replay over LIVE components (paste / prefab revert / undo): when the incoming id
    // differs from the current one, the stale proxy/direct binding is dropped - critically,
    // reading a NIL id actually unbinds instead of leaving the old resource rendering.
    template <typename T>
    void Serialize(ISerializer& ar, Ref<T>& ref)
    {
        const Guid before = ref.id;
        draconic::core::Serialize(ar, ref.id);
        if (ref.id != before) { ref.ClearBinding(); }
    }

    // =======================================================================
    // IResourceFactory - builds a runtime product from a content instance (its
    // source object + data streams). One factory per product type.
    // =======================================================================
    class ResourceManager;   // forward - factories receive it to resolve child resources

    class IResourceFactory
    {
    public:
        virtual ~IResourceFactory() = default;

        [[nodiscard]] virtual const TypeInfo* ProductType() const = 0;
        // Build the runtime product. `manager` lets a composite resource resolve its
        // child resources via manager.Bind<…>(childId) - and doing so AUTOMATICALLY
        // records a dependency edge, so reloading a child reloads this resource too.
        [[nodiscard]] virtual RefPtr<Object> Create(ResourceManager& manager, draconic::content::Instance& instance) = 0;
    };

    // =======================================================================
    // ResourceManager - binds Guids to products over a content database, caching
    // handles and supporting hot reload. Factories are non-owning (registered by
    // the caller).
    // =======================================================================
    class ResourceManager
    {
    public:
        explicit ResourceManager(draconic::content::IContentDatabase& database) noexcept
            : m_database(&database) {}

        void AddFactory(IResourceFactory* factory)
        {
            if (factory != nullptr && factory->ProductType() != nullptr)
            {
                m_factories.InsertOrAssign(factory->ProductType()->id, factory);
            }
        }

        // Binds an id to a handle producing `productType`. Cached by id; a
        // flushed handle is rebuilt in place so existing proxies recover.
        [[nodiscard]] RefPtr<ResourceHandle> Bind(const TypeInfo& productType, const Guid& id)
        {
            // If a factory is mid-build and Binds this id, it's a dependency of the
            // resource currently building: record the edge so a reload propagates.
            if (!m_buildStack.IsEmpty()) { RecordDependency(m_buildStack.Back(), id); }

            if (RefPtr<ResourceHandle>* cached = m_handles.Find(id))
            {
                // COPY the ref out of the map slot BEFORE building: the factory Binds child
                // resources, growing the handle map - a rehash dangles the slot pointer.
                // (The handle OBJECT itself is heap-stable; only the slot moves.)
                RefPtr<ResourceHandle> handle = *cached;
                if (handle->Get() == nullptr) { BuildInto(*handle, productType.id, id); }
                return handle;
            }
            RefPtr<ResourceHandle> handle = MakeRef<ResourceHandle>(DefaultAllocator());
            BuildInto(*handle, productType.id, id);
            m_handles.InsertOrAssign(id, handle);
            return handle;
        }

        template <typename T>
        [[nodiscard]] Proxy<T> Bind(const Guid& id) { return Proxy<T>(Bind(T::StaticType(), id)); }

        template <typename T>
        [[nodiscard]] Proxy<T> Bind(const ResourceId<T>& rid) { return Bind<T>(rid.id); }

        // Rebuilds the product for an already-bound id (e.g. after the source
        // changed on disk) AND, transitively, every resource that depends on it.
        // All proxies see the new products. False if `id` is unbound.
        bool Reload(const Guid& id)
        {
            if (m_handles.Find(id) == nullptr) { return false; }
            Array<Guid> visited;
            ReloadRecursive(id, visited);
            // RE-find: the recursive rebuild binds children, growing the handle map - the
            // pre-reload slot pointer dangles after a rehash. (This was the Sponza crash:
            // the post-cook mass reload of 167 products rehashed mid-cascade.)
            RefPtr<ResourceHandle>* handle = m_handles.Find(id);
            return handle != nullptr && (*handle)->Get() != nullptr;
        }

        // The ids that directly depend on `id` (introspection/tooling). Empty if none.
        // (Edges are recorded automatically when a factory Binds a child mid-build;
        // explicit declaration isn't exposed yet - every planned dependency, incl.
        // include resources, resolves through Bind.)
        [[nodiscard]] Span<const Guid> Dependents(const Guid& id) noexcept
        {
            Array<Guid>* d = m_dependents.Find(id);
            return (d != nullptr) ? Span<const Guid>(d->Data(), d->Size()) : Span<const Guid>{};
        }

        /// Frame tick: ages the graveyard of hot-reloaded-away products and releases the
        /// ones old enough that no in-flight frame can still reference their GPU objects.
        void CollectGarbage()
        {
            // Take the graveyard OUT of the member first: releasing a product runs its
            // destructor, which can RE-ENTER the manager (a dying composite drops child
            // proxies; bookkeeping may push new graves) - mutating m_graveyard while a loop
            // walks it is a use-after-free (the Sponza mass-reload crash: hundreds of
            // products landing in one cook aged out together).
            Array<Grave> graves = Move(m_graveyard);
            m_graveyard = Array<Grave>{};

            Array<Grave> dropped;
            for (Grave& grave : graves)
            {
                if (grave.framesLeft <= 1)
                {
                    dropped.PushBack(Move(grave));
                    continue;
                }
                grave.framesLeft -= 1;
                m_graveyard.PushBack(Move(grave));
            }
            // Destructors run HERE, with m_graveyard consistent; re-entrant pushes append
            // to the fresh member array safely.
            dropped.Clear();
        }

        // Drops the product from a handle without unbinding it; a later Bind/
        // Reload rebuilds it. False if unbound.
        bool Flush(const Guid& id)
        {
            RefPtr<ResourceHandle>* handle = m_handles.Find(id);
            if (handle == nullptr) { return false; }
            (*handle)->Flush();
            return true;
        }

    private:
        void BuildInto(ResourceHandle& handle, TypeId productTypeId, const Guid& id)
        {
            // A rebuild may resolve different children than before; drop the old
            // forward edges so they're re-recorded fresh during this build.
            ClearForwardDeps(id);

            handle.SetProductTypeId(productTypeId);
            // Park the outgoing product instead of destroying it now: GPU products own
            // views/buffers that in-flight frames may still reference - CollectGarbage
            // (ticked by the host once per frame) releases them a few frames later.
            if (Object* old = handle.Get())
            {
                m_graveyard.PushBack(Grave{ RefPtr<Object>(old), kGraveFrames });
            }
            handle.Replace(nullptr);

            draconic::content::Instance* instance = m_database->GetInstance(id);
            if (instance == nullptr) { return; }

            IResourceFactory* const* factory = m_factories.Find(productTypeId);
            if (factory == nullptr) { return; }

            // While `id` is on the build stack, any Bind() the factory makes is
            // recorded as a dependency of `id` (see Bind).
            m_buildStack.PushBack(id);
            handle.Replace((*factory)->Create(*this, *instance));
            m_buildStack.PopBack();
        }

        // Rebuild `id`, then transitively every resource that depends on it. The
        // visited list guards against cycles; dependents are snapshotted because a
        // dependent's rebuild mutates m_dependents[id] (clear+re-record its edges).
        void ReloadRecursive(const Guid& id, Array<Guid>& visited)
        {
            for (const Guid& v : visited) { if (v == id) { return; } }
            visited.PushBack(id);

            if (RefPtr<ResourceHandle>* handle = m_handles.Find(id))
            {
                BuildInto(**handle, (*handle)->ProductTypeId(), id);
            }

            Array<Guid> dependents;
            if (Array<Guid>* d = m_dependents.Find(id))
            {
                for (const Guid& g : *d) { dependents.PushBack(g); }
            }
            for (const Guid& dep : dependents) { ReloadRecursive(dep, visited); }
        }

        void RecordDependency(const Guid& dependent, const Guid& dependency)
        {
            if (dependent == dependency) { return; }
            AddEdgeUnique(m_dependencies, dependent, dependency);
            AddEdgeUnique(m_dependents, dependency, dependent);
        }

        // Drop `id`'s outgoing edges (and the matching reverse entries).
        void ClearForwardDeps(const Guid& id)
        {
            Array<Guid>* deps = m_dependencies.Find(id);
            if (deps == nullptr) { return; }
            for (const Guid& d : *deps)
            {
                if (Array<Guid>* rev = m_dependents.Find(d))
                {
                    for (usize i = 0; i < rev->Size(); ++i)
                    {
                        if ((*rev)[i] == id) { rev->RemoveAt(i); break; }
                    }
                }
            }
            deps->Clear();
        }

        static void AddEdgeUnique(HashMap<Guid, Array<Guid>>& map, const Guid& key, const Guid& value)
        {
            Array<Guid>* arr = map.Find(key);
            if (arr == nullptr) { map.InsertOrAssign(key, Array<Guid>{}); arr = map.Find(key); }
            for (const Guid& g : *arr) { if (g == value) { return; } }
            arr->PushBack(value);
        }

        draconic::content::IContentDatabase* m_database;
        HashMap<TypeId, IResourceFactory*> m_factories;
        HashMap<Guid, RefPtr<ResourceHandle>> m_handles;
        HashMap<Guid, Array<Guid>> m_dependencies;  // id -> resources it depends on
        HashMap<Guid, Array<Guid>> m_dependents;    // id -> resources that depend on it
        Array<Guid> m_buildStack;                   // ids currently building (auto-edge source)

        static constexpr u32 kGraveFrames = 8;      // > max frames in flight, comfortably
        struct Grave
        {
            RefPtr<Object> product;
            u32 framesLeft = 0;
        };
        Array<Grave> m_graveyard;                   // hot-reloaded-away products awaiting release
    };

    template <typename T>
    void Ref<T>::Bind(ResourceManager& manager)
    {
        if (!id.IsNil()) { m_proxy = manager.Bind<T>(id); }
    }
}
