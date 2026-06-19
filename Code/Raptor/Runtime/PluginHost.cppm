// Raptor Runtime — :pluginhost partition
//
// PluginHost: owns the set of loaded plugins and their backing shared libraries
// and drives the load/unload contract against a Context. Statically-created
// plugins are registered with Add() (caller keeps ownership of the object);
// shared-library plugins with Load() (the host owns the library handle and
// closes it on teardown).

module;
#include "Core/Prelude.h"

export module raptor.runtime:pluginhost;

import raptor.core;
import :context;
import :plugin;

namespace rc = raptor::core;

export namespace raptor::runtime
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
            m_entries.PushBack(Entry{ plugin, rc::DynamicLibrary{} });
            return plugin;
        }

        // Loads a plugin from a shared library: resolves the factory, creates the
        // plugin, and calls OnLoad. The library is closed when the host unloads.
        rc::Result<IRuntimePlugin*> Load(rc::WideStringView path)
        {
            rc::DynamicLibrary library;
            if (rc::Status status = library.Load(path); !status)
            {
                return rc::Err(status.Code());
            }

            const auto create = library.GetSymbol<CreatePluginFn>(CreatePluginSymbol);
            if (create == nullptr)
            {
                return rc::Err(rc::ErrorCode::NotFound);
            }

            IRuntimePlugin* plugin = create();
            if (plugin == nullptr)
            {
                return rc::Err(rc::ErrorCode::Internal);
            }

            plugin->OnLoad(*m_context);
            m_entries.PushBack(Entry{ plugin, rc::Move(library) });
            return plugin;
        }

        [[nodiscard]] rc::usize Count() const noexcept { return m_entries.Size(); }

        // Unloads everything in reverse load order: OnUnload each plugin, then
        // close libraries (which may invalidate library-owned plugin objects).
        void UnloadAll()
        {
            for (rc::usize i = m_entries.Size(); i-- > 0;)
            {
                if (m_entries[i].plugin != nullptr) { m_entries[i].plugin->OnUnload(*m_context); }
            }
            m_entries.Clear(); // DynamicLibrary dtors close the shared libraries
        }

    private:
        struct Entry
        {
            IRuntimePlugin* plugin;
            rc::DynamicLibrary library;
        };

        Context* m_context;
        rc::Array<Entry> m_entries;
    };
}
