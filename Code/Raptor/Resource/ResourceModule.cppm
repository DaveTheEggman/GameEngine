// Raptor::Resource — the `raptor.resource` module.
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

export module raptor.resource;

import raptor.core;
import raptor.content;

using namespace raptor::core;

export namespace raptor::resource
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
    // ResourceHandle — a shared, replaceable slot holding one runtime product.
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
    // Proxy<T> — typed accessor over a ResourceHandle. Cheap to copy/store; it
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
    // IResourceFactory — builds a runtime product from a content instance (its
    // source object + data streams). One factory per product type.
    // =======================================================================
    class IResourceFactory
    {
    public:
        virtual ~IResourceFactory() = default;

        [[nodiscard]] virtual const TypeInfo* ProductType() const = 0;
        [[nodiscard]] virtual RefPtr<Object> Create(raptor::content::Instance& instance) = 0;
    };

    // =======================================================================
    // ResourceManager — binds Guids to products over a content database, caching
    // handles and supporting hot reload. Factories are non-owning (registered by
    // the caller).
    // =======================================================================
    class ResourceManager
    {
    public:
        explicit ResourceManager(raptor::content::IContentDatabase& database) noexcept
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
            if (RefPtr<ResourceHandle>* cached = m_handles.Find(id))
            {
                if ((*cached)->Get() == nullptr) { BuildInto(**cached, productType.id, id); }
                return *cached;
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
        // changed on disk). All proxies see the new product. False if unbound.
        bool Reload(const Guid& id)
        {
            RefPtr<ResourceHandle>* handle = m_handles.Find(id);
            if (handle == nullptr) { return false; }
            BuildInto(**handle, (*handle)->ProductTypeId(), id);
            return (*handle)->Get() != nullptr;
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
            handle.SetProductTypeId(productTypeId);
            handle.Replace(nullptr);

            raptor::content::Instance* instance = m_database->GetInstance(id);
            if (instance == nullptr) { return; }

            IResourceFactory* const* factory = m_factories.Find(productTypeId);
            if (factory == nullptr) { return; }

            handle.Replace((*factory)->Create(*instance));
        }

        raptor::content::IContentDatabase* m_database;
        HashMap<TypeId, IResourceFactory*> m_factories;
        HashMap<Guid, RefPtr<ResourceHandle>> m_handles;
    };
}
