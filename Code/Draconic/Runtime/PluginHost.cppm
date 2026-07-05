// Draconic Runtime — :pluginhost partition
//
// PluginHost: owns the set of loaded plugins and their backing shared libraries
// and drives the load/unload contract against a Context. Statically-created
// plugins are registered with Add() (caller keeps ownership of the object);
// shared-library plugins with Load() (the host owns the library handle and
// closes it on teardown).

module;
#include "Core/Prelude.h"

export module draconic.runtime:pluginhost;

import draconic.core;
import :context;
import :plugin;

namespace core = draconic::core;

export namespace draconic::runtime
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
            if (plugin == nullptr) { return nullptr; }
            plugin->OnLoad(*m_context);
            m_entries.PushBack(Entry{ plugin, core::DynamicLibrary{} });
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

            plugin->OnLoad(*m_context);
            m_entries.PushBack(Entry{ plugin, core::Move(library) });
            return plugin;
        }

        [[nodiscard]] core::usize Count() const noexcept { return m_entries.Size(); }

        // Unloads everything in reverse load order: OnUnload each plugin, then
        // close libraries (which may invalidate library-owned plugin objects).
        void UnloadAll()
        {
            for (core::usize i = m_entries.Size(); i-- > 0;)
            {
                if (m_entries[i].plugin != nullptr) { m_entries[i].plugin->OnUnload(*m_context); }
            }
            m_entries.Clear(); // DynamicLibrary dtors close the shared libraries
        }

    private:
        struct Entry
        {
            IRuntimePlugin* plugin;
            core::DynamicLibrary library;
        };

        Context* m_context;
        core::Array<Entry> m_entries;
    };
}
