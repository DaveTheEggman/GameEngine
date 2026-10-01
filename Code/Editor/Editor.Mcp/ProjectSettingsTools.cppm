// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :project_settings partition (Sedulous 3de51786)
//
// The open project's settings over MCP: what project_info reports and project_settings_set
// changes. Play reads its default scene, startup script and input map from them, so an agent's
// game binds its input map here. Both read the settings from ProjectSettings' reflection, the
// description the Project Settings dialog builds its rows from: a labelled string setting is
// text, an asset setting a guid that must name an asset of the type it takes. MSAA is the one
// setting with a rule of its own: a level of the render subsystem's table.

module;
#include "Core/Prelude.h"

export module editor.mcp:project_settings;

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.mcp;
import engine.project;
import engine.render; // the canonical MSAA level table
import editor.project;
import :session;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;

export namespace editor::mcp::detail
{
    /// A number setting's value as a JSON number.
    inline JsonValue ValueNumber(const Variant& value)
    {
        if (value.Is<u32>())
        {
            return JsonValue::MakeNumber(static_cast<f64>(value.Get<u32>()));
        }
        if (value.Is<i32>())
        {
            return JsonValue::MakeNumber(static_cast<f64>(value.Get<i32>()));
        }
        return JsonValue::MakeNull();
    }

    /// The settings that are settings: the reflected properties with a label, of the kinds a
    /// setting takes (a string, an asset's guid, a count).
    inline Array<const PropertyInfo*> ProjectSettingProperties()
    {
        Array<const PropertyInfo*> out;
        for (const PropertyInfo& property : Properties(engine::project::ProjectSettings::StaticType()))
        {
            const bool kind = property.type == &TypeOf<String>() ||
                              property.type == &TypeOf<u32>() ||
                              (property.type == &TypeOf<Guid>() &&
                               engine::project::SettingAttribute(
                                   property, engine::project::kSettingAssetTypeAttribute) != nullptr);
            if (kind && engine::project::SettingAttribute(
                            property, engine::project::kSettingLabelAttribute) != nullptr)
            {
                out.PushBack(&property);
            }
        }
        return out;
    }

    inline StringView PropertyName(const PropertyInfo& property)
    {
        return StringView(reinterpret_cast<const utf8char*>(property.name));
    }

    /// The settings as project_info reports them: an asset setting {guid, path} (null when unset),
    /// a string as text, a number as a number.
    inline JsonValue ProjectSettingsJson(editor::EditorProject& project)
    {
        engine::project::ProjectSettings& settings = project.Settings();
        const Instance instance(&settings, &engine::project::ProjectSettings::StaticType());
        JsonValue out = JsonValue::MakeObject();
        for (const PropertyInfo* property : ProjectSettingProperties())
        {
            const String key(PropertyName(*property));
            void* address = property->address(instance);
            if (engine::project::SettingAttribute(*property,
                                                  engine::project::kSettingAssetTypeAttribute))
            {
                const Guid& id = *static_cast<const Guid*>(address);
                if (id.IsNil())
                {
                    out.Set(key, JsonValue::MakeNull());
                    continue;
                }
                content::Instance* asset = project.SourceDb().GetInstance(id);
                JsonValue entry = JsonValue::MakeObject();
                entry.Set(u8"guid", GuidToJson(id));
                entry.Set(u8"path", asset != nullptr ? JsonValue::MakeString(asset->Path())
                                                     : JsonValue::MakeNull());
                out.Set(key, Move(entry));
            }
            else if (property->type == &TypeOf<String>())
            {
                out.Set(key, JsonValue::MakeString(*static_cast<const String*>(address)));
            }
            else
            {
                out.Set(key, ValueNumber(GetProperty(*property, instance)));
            }
        }
        return out;
    }

    /// project_settings_set's schema: one argument per setting, as reflection describes it.
    inline JsonValue ProjectSettingsSchema()
    {
        foundation::mcp::SchemaBuilder schema;
        for (const PropertyInfo* property : ProjectSettingProperties())
        {
            const String* label =
                engine::project::SettingAttribute(*property, engine::project::kSettingLabelAttribute);
            const String key(PropertyName(*property));
            if (const String* assetType = engine::project::SettingAttribute(
                    *property, engine::project::kSettingAssetTypeAttribute))
            {
                const String* emptyText = engine::project::SettingAttribute(
                    *property, engine::project::kSettingEmptyTextAttribute);
                schema.Str(key, Format(u8"{}: a {}'s guid; \"\" clears it ({})", label->AsView(),
                                       assetType->AsView(),
                                       emptyText != nullptr ? emptyText->AsView() : StringView()));
            }
            else if (property->type == &TypeOf<String>())
            {
                schema.Str(key, *label);
            }
            else
            {
                schema.Integer(key, *label);
            }
        }
        return schema.Build();
    }

}

export namespace editor::mcp
{
    inline void RegisterProjectSettingsTool(foundation::mcp::McpServer& server,
                                            ProjectSession& session)
    {
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;
        server.RegisterTool(
            u8"project_settings_set",
            u8"Change the open project's settings, what the editor's Project Settings dialog "
            u8"edits: only what is given changes. Every asset setting must name an asset of its "
            u8"type (the refusal says which), \"\" clears it; MSAA takes the render levels. Checked "
            u8"in full before anything changes, then saved to the manifest; the editor re-applies "
            u8"what depends on them (the game UI's font and theme). Returns the settings as "
            u8"project_info does.",
            detail::ProjectSettingsSchema(), foundation::mcp::ToolAnnotations::Adjusts(),
            [s](const JsonValue& args) -> ToolResult
            {
                if (s->project == nullptr)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                editor::EditorProject& project = *s->project;
                engine::project::ProjectSettings& settings = project.Settings();
                const Instance instance(&settings, &engine::project::ProjectSettings::StaticType());
                const Array<const PropertyInfo*> properties = detail::ProjectSettingProperties();

                // Unknown names first: a misspelled setting is refused, not ignored.
                for (const String& key : args.Keys())
                {
                    bool known = false;
                    for (const PropertyInfo* property : properties)
                    {
                        known = known || detail::PropertyName(*property) == key.AsView();
                    }
                    if (!known)
                    {
                        String names;
                        for (const PropertyInfo* property : properties)
                        {
                            names += names.IsEmpty() ? u8"" : u8", ";
                            names += detail::PropertyName(*property);
                        }
                        return Err(Format(u8"no setting '{}'; the settings are: {}", key.AsView(),
                                          names.AsView()));
                    }
                }

                // Everything is checked before anything changes: a refusal leaves the settings
                // as they were.
                struct Change
                {
                    const PropertyInfo* property = nullptr;
                    Guid id;
                    String text;
                    u32 number = 0;
                };
                Array<Change> changes;
                for (const PropertyInfo* property : properties)
                {
                    const String key(detail::PropertyName(*property));
                    if (!args.Has(key))
                    {
                        continue;
                    }
                    const JsonValue value = args.Get(key);
                    Change change;
                    change.property = property;
                    if (const String* assetType = engine::project::SettingAttribute(
                            *property, engine::project::kSettingAssetTypeAttribute))
                    {
                        const String text = value.AsString();
                        if (!text.IsEmpty())
                        {
                            if (!Guid::TryParse(text.AsView(), change.id))
                            {
                                return Err(Format(u8"`{}`: '{}' is not a valid guid", key.AsView(),
                                                  text.AsView()));
                            }
                            content::Instance* asset = project.SourceDb().GetInstance(change.id);
                            if (asset == nullptr)
                            {
                                return Err(Format(u8"`{}`: no asset with guid {} in the project",
                                                  key.AsView(), text.AsView()));
                            }
                            if (asset->TypeName() != assetType->AsView())
                            {
                                return Err(Format(u8"`{}` takes a {}; '{}' is a {}", key.AsView(),
                                                  assetType->AsView(), asset->Name(),
                                                  asset->TypeName()));
                            }
                        }
                    }
                    else if (property->type == &TypeOf<String>())
                    {
                        change.text = value.AsString();
                    }
                    else
                    {
                        if (!value.IsNumber() || value.AsNumber() < 0.0)
                        {
                            return Err(Format(u8"`{}` takes a count from 0", key.AsView()));
                        }
                        change.number = static_cast<u32>(value.AsInt());
                        // MSAA takes a level of the render subsystem's table.
                        if (property->address(instance) == &settings.renderMsaaSamples)
                        {
                            bool level = false;
                            String levels;
                            for (u32 i = 0; i < engine::render::MsaaLevelCount(); ++i)
                            {
                                level = level || engine::render::kMsaaLevels[i].samples == change.number;
                                levels += levels.IsEmpty() ? u8"" : u8", ";
                                levels += Format(u8"{}", engine::render::kMsaaLevels[i].samples);
                            }
                            if (!level)
                            {
                                return Err(Format(u8"`{}` takes {}", key.AsView(), levels.AsView()));
                            }
                        }
                    }
                    changes.PushBack(Move(change));
                }

                for (const Change& change : changes)
                {
                    void* address = change.property->address(instance);
                    if (engine::project::SettingAttribute(*change.property,
                                                          engine::project::kSettingAssetTypeAttribute))
                    {
                        *static_cast<Guid*>(address) = change.id;
                    }
                    else if (change.property->type == &TypeOf<String>())
                    {
                        *static_cast<String*>(address) = change.text;
                    }
                    else
                    {
                        *static_cast<u32*>(address) = change.number;
                    }
                }
                // The path mirrors the dialog keeps beside the guids.
                settings.RefreshPathMirrors(
                    [&project](const Guid& id)
                    {
                        content::Instance* asset = project.SourceDb().GetInstance(id);
                        return asset != nullptr ? asset->Path() : String();
                    });
                if (!project.SaveSettings().IsOk())
                {
                    return Err(String(u8"the settings changed but the manifest did not save "
                                      u8"(log_read says why)"));
                }
                if (s->onSettingsChanged)
                {
                    s->onSettingsChanged();
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"settings", detail::ProjectSettingsJson(project));
                return out;
            });
    }
}
