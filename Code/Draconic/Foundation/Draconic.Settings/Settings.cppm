// Draconic::Settings - the `draconic.settings` module.
//
// A typed, versioned, backend-agnostic settings store (see docs/design/settings.md). A "section" is
// a reflected ISerializable struct; the store holds one instance per type, lazily created (so an
// absent section reads as its struct defaults) and (de)serialized as a versioned payload keyed by the
// type's namespace+name. Load/Save go through the ISerializer/SerializerFactory abstraction - the
// CALLER picks the backend (XML for hand-editable files, binary for tests): the store is not tied to
// any one. Streams only (no VFS): the caller opens the file (e.g. via VFS at
// core::GetUserDataDirectory()) and hands over the stream. Sits just above Core so the runtime and
// the editor can both use it.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.settings;

import draconic.core;

using namespace draconic::core;

export namespace draconic::settings
{
    // A store of typed settings sections. One instance = one persisted file's worth (layering across
    // user/project files composes multiple Settings; a later phase). Not copyable (owns live sections).
    class Settings
    {
    public:
        Settings() = default;
        Settings(const Settings&) = delete;
        Settings& operator=(const Settings&) = delete;

        // The section of type T, lazily created (struct defaults) on first access. Stable reference
        // for the store's lifetime. T must be an ISerializable with a default ctor (DRACONIC_OBJECT).
        template <typename T>
        [[nodiscard]] T& Section()
        {
            const TypeId id = T::StaticType().id;
            if (RefPtr<ISerializable>* existing = m_sections.Find(id))
            {
                return static_cast<T&>(*existing->Get());
            }
            RefPtr<ISerializable> obj = MakeRef<T>(DefaultAllocator());
            T& ref = static_cast<T&>(*obj.Get());
            m_sections.InsertOrAssign(id, static_cast<RefPtr<ISerializable>&&>(obj));
            return ref;
        }

        // The section of type T if present (loaded or previously accessed); null otherwise - a
        // read-only peek that does NOT create the section.
        template <typename T>
        [[nodiscard]] const T* Find() const
        {
            const RefPtr<ISerializable>* existing = m_sections.Find(T::StaticType().id);
            return (existing != nullptr) ? static_cast<const T*>(existing->Get()) : nullptr;
        }

        // Announce that section T was mutated: fires OnChanged with T's type name. (The store can't
        // observe field writes through a T& reference, so callers signal explicitly.)
        template <typename T>
        void MarkChanged()
        {
            if (m_onChanged)
            {
                m_onChanged(StringView(reinterpret_cast<const utf8char*>(T::StaticType().name)));
            }
        }

        void OnChanged(Function<void(StringView)> cb)
        {
            m_onChanged = static_cast<Function<void(StringView)>&&>(cb);
        }

        [[nodiscard]] usize SectionCount() const noexcept { return m_sections.Size(); }

        // Serialize every live section to `out` through `factory` (envelope per section: type
        // namespace + name + versioned payload). Backend chosen by the caller.
        [[nodiscard]] Status Save(IStream& out, SerializerFactory factory) const
        {
            UniquePtr<SerializerContext> ctx = factory(out, SerializeMode::Write);
            if (!ctx || ctx->serializer == nullptr)
            {
                return Status{ErrorCode::Internal};
            }
            auto& ar =
                *ctx->serializer; // concrete Serializer (has IsOk/GetStatus, not on ISerializer)

            u32 count = static_cast<u32>(m_sections.Size());
            ar.Key("sections");
            ar.BeginArray(count);
            for (const auto& kv : m_sections)
            {
                ISerializable* obj = kv.value.Get();
                const TypeInfo& t = *obj->GetType();
                ar.BeginObject();
                String ns(reinterpret_cast<const utf8char*>(t.namespaceName));
                String name(reinterpret_cast<const utf8char*>(t.name));
                ar.Key("typeNamespace");
                ar.Text(ns);
                ar.Key("typeName");
                ar.Text(name);
                BeginVersionedPayload(ar, t);
                ar.Key("payload");
                ar.BeginObject();
                obj->Serialize(ar);
                ar.EndObject();
                EndVersionedPayload(ar);
                ar.EndObject();
            }
            ar.EndArray();
            if (!ar.IsOk())
            {
                return ar.GetStatus();
            }
            ctx->Flush(out);
            return Status{};
        }

        // Load sections from `in`. Each known+registered section type is instantiated and
        // deserialized; the store replaces any existing instance of that type. NOTE (Phase 1): a
        // section whose type is unknown to this build cannot be skipped on a positional backend, so
        // it aborts the load (unknown-section passthrough is a later phase - docs/design/settings.md
        // §3.4). Registered types must have called RegisterSerializable<T>().
        [[nodiscard]] Status
        Load(IStream& in, SerializerFactory factory, TypeRegistry& types = GlobalTypeRegistry(),
             SerializableRegistry& serializables = GlobalSerializableRegistry())
        {
            UniquePtr<SerializerContext> ctx = factory(in, SerializeMode::Read);
            if (!ctx || ctx->serializer == nullptr)
            {
                return Status{ErrorCode::Internal};
            }
            auto& ar =
                *ctx->serializer; // concrete Serializer (has IsOk/GetStatus, not on ISerializer)

            u32 count = 0;
            ar.Key("sections");
            ar.BeginArray(count);
            for (u32 i = 0; i < count; ++i)
            {
                ar.BeginObject();
                String ns;
                String name;
                ar.Key("typeNamespace");
                ar.Text(ns);
                ar.Key("typeName");
                ar.Text(name);
                if (!ar.IsOk())
                {
                    return ar.GetStatus();
                }

                const TypeInfo* type = types.FindByName(reinterpret_cast<const char*>(ns.CStr()),
                                                        reinterpret_cast<const char*>(name.CStr()));
                RefPtr<ISerializable> obj =
                    (type != nullptr) ? serializables.Create(type->id) : RefPtr<ISerializable>{};
                if (obj.Get() == nullptr)
                {
                    // Unknown/unregistered type: can't skip an unknown-shape payload on a positional
                    // backend, so abort (unknown-section passthrough is a later phase; §3.4). The
                    // caller surfaces the error.
                    return Status{ErrorCode::NotSupported};
                }

                BeginVersionedPayload(ar, *type);
                ar.Key("payload");
                ar.BeginObject();
                obj->Serialize(ar);
                ar.EndObject();
                EndVersionedPayload(ar);
                ar.EndObject();
                if (!ar.IsOk())
                {
                    return ar.GetStatus();
                }

                m_sections.InsertOrAssign(type->id, static_cast<RefPtr<ISerializable>&&>(obj));
            }
            ar.EndArray();
            return ar.IsOk() ? Status{} : ar.GetStatus();
        }

    private:
        HashMap<TypeId, RefPtr<ISerializable>> m_sections;
        Function<void(StringView)> m_onChanged;
    };
}
