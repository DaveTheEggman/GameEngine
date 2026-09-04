// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Runtime - :pluginhost partition
//
// PluginHost: owns the set of loaded plugins and their backing shared libraries
// and drives the load/unload contract against a Context. Statically-created
// plugins are registered with Add() (caller keeps ownership of the object);
// shared-library plugins with Load() (the host owns the library handle and
// closes it on teardown).

module;
#include "Core/Prelude.h"

export module foundation.runtime:pluginhost;

import foundation.core;
import :context;
import :plugin;

namespace core = foundation::core;

export namespace foundation::runtime
{
    class PluginHost
    {
    public:
        explicit PluginHost(Context& context) noexcept : m_context(&context) {}
        ~PluginHost() { UnloadAll(); }

        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        // Registers an already-created plugin (the caller retains ownership of the
        // object) and calls OnLoad immediately. Returns the plugin for chaining.
        IRuntimePlugin* Add(IRuntimePlugin* plugin)
        {
            if (plugin == nullptr)
            {
                return nullptr;
            }
            Entry entry{plugin, core::DynamicLibrary{}, {}, {}};
            RecordedOnLoad(entry);
            m_entries.PushBack(core::Move(entry));
            return plugin;
        }

        // Loads a plugin from a shared library: resolves the factory, creates the
        // plugin, and calls OnLoad. The library is closed when the host unloads.
        core::Result<IRuntimePlugin*> Load(core::StringView path)
        {
            core::DynamicLibrary library;
            if (core::Status status = library.Load(path); !status)
            {
                return core::Err(status.Code());
            }

            const auto create = library.GetSymbol<CreatePluginFn>(CreatePluginSymbol);
            if (create == nullptr)
            {
                return core::Err(core::ErrorCode::NotFound);
            }

            IRuntimePlugin* plugin = create();
            if (plugin == nullptr)
            {
                return core::Err(core::ErrorCode::Internal);
            }

            Entry entry{plugin, core::Move(library), {}, {}};
            RecordedOnLoad(entry);
            m_entries.PushBack(core::Move(entry));
            return plugin;
        }

        [[nodiscard]] core::usize Count() const noexcept { return m_entries.Size(); }

        // Unloads everything in reverse load order: OnUnload each plugin, reverse its
        // RECORDED registrations (the engine-verified half of teardown - a registry
        // entry or factory pointing into a closed library is a dangling read), then
        // close libraries (which may invalidate library-owned plugin objects).
        void UnloadAll()
        {
            for (core::usize i = m_entries.Size(); i-- > 0;)
            {
                if (m_entries[i].plugin != nullptr)
                {
                    m_entries[i].plugin->OnUnload(*m_context);
                }
                ReverseRecorded(m_entries[i]);
            }
            m_entries.Clear(); // DynamicLibrary dtors close the shared libraries
        }

    private:
        struct Entry
        {
            IRuntimePlugin* plugin;
            core::DynamicLibrary library;
            // Everything OnLoad registered, recorded by the ambient observers below -
            // reversed on unload so the plugin never hand-mirrors its registrations
            // (game-native-code.md N6 RegistrationScope).
            core::Array<core::TypeId> registeredTypes;
            core::Array<core::TypeId> registeredSerializables;
        };

        // The RegistrationScope: arm ambient observers on the global registries for the
        // duration of OnLoad. Observers fire only on REAL inserts, so a type another
        // party already owns is never recorded (and never torn down by this plugin).
        void RecordedOnLoad(Entry& entry)
        {
            struct Capture
            {
                core::Array<core::TypeId>* types;
                core::Array<core::TypeId>* serializables;
            } capture{&entry.registeredTypes, &entry.registeredSerializables};
            core::GlobalTypeRegistry().SetRegistrationObserver(
                [](void* ctx, core::TypeId id)
                { static_cast<Capture*>(ctx)->types->PushBack(id); },
                &capture);
            core::GlobalSerializableRegistry().SetRegistrationObserver(
                [](void* ctx, core::TypeId id)
                { static_cast<Capture*>(ctx)->serializables->PushBack(id); },
                &capture);
            entry.plugin->OnLoad(*m_context);
            core::GlobalTypeRegistry().SetRegistrationObserver(nullptr, nullptr);
            core::GlobalSerializableRegistry().SetRegistrationObserver(nullptr, nullptr);
        }

        void ReverseRecorded(Entry& entry)
        {
            for (core::usize i = entry.registeredSerializables.Size(); i-- > 0;)
            {
                core::GlobalSerializableRegistry().Unregister(entry.registeredSerializables[i]);
            }
            for (core::usize i = entry.registeredTypes.Size(); i-- > 0;)
            {
                core::GlobalTypeRegistry().Unregister(entry.registeredTypes[i]);
            }
            entry.registeredTypes.Clear();
            entry.registeredSerializables.Clear();
        }

        Context* m_context;
        core::Array<Entry> m_entries;
    };
}
