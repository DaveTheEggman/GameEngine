// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The editor's USER-LEVEL settings store (<user-data>/editor.settings.xml): the
// cross-project preference sections (fonts, UI scale), the file plumbing, and the
// one registration entry point for every section type.
//
// Section types may live where their DOMAIN lives (EditorExportSettings stays with
// the export code, RecentProjectsSettings with the project registry) - but they are
// all REGISTERED here, in one place, because a section registered without its
// serializable factory makes Settings::Load abort the whole store at that section
// and silently drop everything after it. One list, both calls,
// every type - see RegisterEditorSettingsTypes.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.core:editor_settings;

import foundation.core;
import foundation.vfs;
import foundation.xml.serialization;
import foundation.settings;
import :export_preset; // EditorExportSettings (registered below)
import :actions;       // EditorShortcutSettings captures from and applies to the registry

using namespace foundation::core;

export namespace editor
{
    namespace settings = foundation::settings;
    namespace vfs = foundation::vfs;

    // Editor-level FONT preferences (a Settings section): explicit .ttf paths for the UI
    // and mono families. Empty (the default) = the built-in resolution chain (the dev-tree
    // compile define, then the exe-embedded fallback face).
    class EditorFontSettings final : public ISerializable
    {
        RTTI_OBJECT(EditorFontSettings, ISerializable)
    public:
        String fontPath;     // UI family override ("" = built-in chain)
        String monoFontPath; // mono family override ("" = built-in chain)

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "fontPath", fontPath);
            foundation::core::Serialize(ar, "monoFontPath", monoFontPath);
        }
    };

    // The UI scale preference's range: 50% (a dense layout on a large monitor) to 200%.
    inline constexpr f32 kUiScaleMin = 0.5f;
    inline constexpr f32 kUiScaleMax = 2.0f;

    // Editor-level UI preferences (a Settings section). uiScale multiplies the window's
    // OS content scale for the whole editor UI (layout + fonts + baked icons) - both an
    // accessibility knob and the way to exercise the DPI path without a scaled monitor.
    class EditorUiSettings final : public ISerializable
    {
        RTTI_OBJECT(EditorUiSettings, ISerializable)
    public:
        f32 uiScale = 1.0f; // clamped to [kUiScaleMin, kUiScaleMax] on use

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "uiScale", uiScale);
        }
    };

    // The editor's settings file, in the user-data dir (hand-editable XML, like the project files).
    inline constexpr StringView kEditorSettingsFile = u8"editor.settings.xml";

    // Register the editor's Settings section types so a Settings store can instantiate them on Load.
    // Call once at editor startup, before LoadEditorSettings.
    // The editor's MCP host - agent access to the OPEN project over localhost HTTP. Off by
    // default. `port` is where it listens (one editor per port: a second editor takes
    // another); `token` is the bearer secret the editor generates on first enable (see
    // GenerateMcpToken) and also writes to <user-data>/mcp-token so a local agent
    // self-configures. Edited in Preferences; `--mcp` / `--mcp-port` override one run.
    inline constexpr u32 kEditorMcpDefaultPort = 7405;
    class EditorMcpSettings final : public ISerializable
    {
        RTTI_OBJECT(EditorMcpSettings, ISerializable)
    public:
        bool enabled = false;
        u32 port = kEditorMcpDefaultPort;
        String token;

        /// The Preferences fields as typed. `portText` must name a port in 1024..65535 (below
        /// needs elevation, 0 cannot be put in a client's URL): anything else leaves the port
        /// as it was and returns false. `token` is taken verbatim; empty means "mint a new one
        /// on the next enable".
        bool ApplyFromPreferences(bool enable, StringView portText, StringView newToken)
        {
            enabled = enable;
            token = String(newToken);
            const Optional<i64> parsed = ParseInt(portText);
            if (!parsed.HasValue() || parsed.Value() < 1024 || parsed.Value() > 65535)
            {
                return false;
            }
            port = static_cast<u32>(parsed.Value());
            return true;
        }

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "enabled", enabled);
            foundation::core::Serialize(ar, "port", port);
            foundation::core::Serialize(ar, "token", token);
        }
    };

    // A fresh bearer token for the MCP host: a Guid from OS entropy, in its canonical text (36
    // chars) - a secret, so nothing seeded by the clock, which a neighbour can guess. Only
    // when the OS refuses entropy does a clock-and-process seed stand in, with a warning.
    [[nodiscard]] inline String GenerateMcpToken()
    {
        Guid secret;
        if (!Guid::TryGenerateFromSystemEntropy(secret))
        {
            LOG_WARNING(u8"Editor", u8"the OS gave no entropy for the MCP token; minted from the "
                                    u8"clock and the process id instead - treat it as guessable");
            Random rng(GetTicks() ^ (static_cast<u64>(ProcessId()) << 32));
            secret = Guid::Generate(rng);
        }
        utf8char text[37];
        secret.ToChars(text);
        return String(StringView(text, 36));
    }

    // One shortcut the user rebound: the action's id and the chord (an unset chord - key 0 -
    // is "no shortcut", on purpose, distinct from the declaration's default).
    struct ShortcutOverrideEntry
    {
        String id;
        u32 key = 0;
        u32 modifiers = 0;

        void Serialize(ISerializer& ar)
        {
            foundation::core::Serialize(ar, "id", id);
            foundation::core::Serialize(ar, "key", key);
            foundation::core::Serialize(ar, "modifiers", modifiers);
        }
        [[nodiscard]] EditorShortcut Chord() const noexcept
        {
            return EditorShortcut{static_cast<foundation::ui::KeyCode>(key),
                                  static_cast<foundation::ui::KeyModifiers>(modifiers)};
        }
    };
    inline void Serialize(ISerializer& ar, ShortcutOverrideEntry& e)
    {
        ar.BeginObject();
        e.Serialize(ar);
        ar.EndObject();
    }

    // The user's shortcut overrides (Preferences > Shortcuts), keyed by action id. Captured
    // from the registry on Save, applied to it after every domain has registered its actions
    // (an id nobody registered - a domain not loaded in this build - stays in the section
    // untouched, so a later run still has it).
    class EditorShortcutSettings final : public ISerializable
    {
        RTTI_OBJECT(EditorShortcutSettings, ISerializable)
    public:
        Array<ShortcutOverrideEntry> overrides;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "overrides", overrides);
        }
        [[nodiscard]] const ShortcutOverrideEntry* Find(StringView id) const
        {
            for (const ShortcutOverrideEntry& e : overrides)
            {
                if (e.id.AsView() == id)
                {
                    return &e;
                }
            }
            return nullptr;
        }
    };

    /// The registry's overrides into the section: every registered action with one is written
    /// (added or updated); an entry for an id the registry does not know is kept. Returns how
    /// many entries the section holds after.
    inline usize CaptureShortcutOverrides(const EditorActionRegistry& actions, EditorShortcutSettings& out)
    {
        for (const EditorActionDeclaration& action : actions.Actions())
        {
            const StringView id = action.id.AsView();
            const bool has = actions.HasOverride(id);
            ShortcutOverrideEntry* entry = nullptr;
            for (ShortcutOverrideEntry& e : out.overrides)
            {
                if (e.id.AsView() == id)
                {
                    entry = &e;
                    break;
                }
            }
            if (!has)
            {
                if (entry != nullptr)
                {
                    const usize index = static_cast<usize>(entry - out.overrides.Data());
                    out.overrides.RemoveAt(index); // reset to the default: the entry goes
                }
                continue;
            }
            const EditorShortcut chord = actions.Shortcut(id);
            if (entry == nullptr)
            {
                ShortcutOverrideEntry fresh;
                fresh.id = String(id);
                out.overrides.PushBack(Move(fresh));
                entry = &out.overrides[out.overrides.Size() - 1];
            }
            entry->key = static_cast<u32>(chord.key);
            entry->modifiers = static_cast<u32>(chord.modifiers);
        }
        return out.overrides.Size();
    }

    /// The section's overrides into the registry, entry by entry through Rebind: an unknown id
    /// is skipped silently (a domain not loaded), a collision is logged and skipped (the other
    /// action keeps the chord). Returns how many bound.
    inline usize ApplyShortcutOverrides(const EditorShortcutSettings& in, EditorActionRegistry& actions)
    {
        usize applied = 0;
        for (const ShortcutOverrideEntry& e : in.overrides)
        {
            const EditorActionDeclaration* holder = nullptr;
            const Status bound = actions.Rebind(e.id.AsView(), e.Chord(), &holder);
            if (bound.IsOk())
            {
                ++applied;
            }
            else if (bound.Code() == ErrorCode::AlreadyExists && holder != nullptr)
            {
                LOG_WARNING(u8"Editor", u8"shortcut override for '{}' skipped: '{}' holds {}",
                            e.id.AsView(), holder->id.AsView(), FormatShortcut(e.Chord()).AsView());
            }
        }
        return applied;
    }

    inline void RegisterEditorSettingsTypes()
    {
        // EVERY section type needs BOTH registrations: the type (so Load can match the
        // stored name) AND the serializable factory (so Load can instantiate it). A type
        // registered without its factory is a time bomb: the first file SAVED with that
        // section makes every later Load abort mid-file (Settings phase-1 semantics),
        // silently dropping the sections after it - the empty-project-list incident.
        GlobalTypeRegistry().Register(EditorExportSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<EditorExportSettings>();
        GlobalTypeRegistry().Register(EditorFontSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<EditorFontSettings>();
        GlobalTypeRegistry().Register(EditorUiSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<EditorUiSettings>();
        GlobalTypeRegistry().Register(EditorMcpSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<EditorMcpSettings>();
        GlobalTypeRegistry().Register(EditorShortcutSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<EditorShortcutSettings>();
    }

    // Load the editor settings store from `root` (XML). NotFound when the file is absent (first run =>
    // the store stays empty and every section reads as its defaults). Types must be registered first.
    [[nodiscard]] inline Status LoadEditorSettings(vfs::IFileSystem& root, settings::Settings& out,
                                                   StringView fileName = kEditorSettingsFile)
    {
        UniquePtr<IStream> stream = root.Open(fileName, FileMode::Read);
        if (!stream)
        {
            return Status{ErrorCode::NotFound};
        }
        return out.Load(*stream, foundation::xml::XmlSerializerFactory());
    }

    // Persist the editor settings store to `root` (XML).
    [[nodiscard]] inline Status SaveEditorSettings(vfs::IWritableFileSystem& root,
                                                   const settings::Settings& in,
                                                   StringView fileName = kEditorSettingsFile)
    {
        MemoryStream buffer;
        if (Status s = in.Save(buffer, foundation::xml::XmlSerializerFactory()); !s.IsOk())
        {
            return s;
        }
        return root.Save(fileName, buffer.Bytes());
    }

    // Load/save the editor settings at their canonical location (<user-data>/editor.settings.xml).
    // The editor uses these; tests use the fs-explicit forms above.
    [[nodiscard]] inline Status LoadEditorSettingsFromUserData(settings::Settings& out)
    {
        vfs::NativeFileSystem fs(GetUserDataDirectory().AsView(), foundation::core::DefaultAllocator());
        return LoadEditorSettings(fs, out);
    }
    [[nodiscard]] inline Status SaveEditorSettingsToUserData(const settings::Settings& in)
    {
        const String dir = GetUserDataDirectory();
        (void)CreateDirectory(dir.AsView()); // ensure the leaf dir exists before writing
        vfs::NativeFileSystem fs(dir.AsView(), foundation::core::DefaultAllocator());
        return SaveEditorSettings(*fs.AsWritable(), in);
    }

    RTTI_DEFINE_OBJECT_VERSIONED(EditorFontSettings, "rtti::editor::editor", 1)
    RTTI_DEFINE_OBJECT_VERSIONED(EditorUiSettings, "rtti::editor::editor", 1)
    RTTI_DEFINE_OBJECT_VERSIONED(EditorMcpSettings, "rtti::editor::editor", 1)
    RTTI_DEFINE_OBJECT_VERSIONED(EditorShortcutSettings, "rtti::editor::editor", 1)
}
