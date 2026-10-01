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
    /// setting takes (a string, an asset's guid or a list of them, a count, a flag, a choice).
    inline Array<const PropertyInfo*> ProjectSettingProperties()
    {
        Array<const PropertyInfo*> out;
        for (const PropertyInfo& property : Properties(engine::project::ProjectSettings::StaticType()))
        {
            const bool kind = property.type == &TypeOf<String>() ||
                              property.type == &TypeOf<u32>() || property.type == &TypeOf<bool>() ||
                              IsEnum(*property.type) ||
                              engine::project::IsAssetSetting(property) ||
                              engine::project::IsAssetListSetting(property);
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

    /// A count setting's bounds: its reflected `range` (min, max), else every u32.
    inline void CountRange(const PropertyInfo& property, u32& least, u32& most)
    {
        least = 0;
        most = 0xFFFFFFFFu;
        if (const Attribute* range = FindAttribute(property, u8"range"))
        {
            if (const Float4* bounds = range->value.TryGet<Float4>())
            {
                least = static_cast<u32>(bounds->x);
                most = static_cast<u32>(bounds->y);
            }
        }
    }

    /// A choice setting's values by name, comma separated (what a refusal and the schema list).
    inline String EnumNames(const TypeInfo& type)
    {
        String names;
        for (const EnumValue& value : Enumerators(type))
        {
            names += names.IsEmpty() ? u8"" : u8", ";
            names += StringView(reinterpret_cast<const utf8char*>(value.name));
        }
        return names;
    }

    /// An asset a setting names as {guid, path}; the path is null when the guid names nothing.
    inline JsonValue SettingAssetJson(editor::EditorProject& project, const Guid& id)
    {
        content::Instance* asset = project.SourceDb().GetInstance(id);
        JsonValue entry = JsonValue::MakeObject();
        entry.Set(u8"guid", GuidToJson(id));
        entry.Set(u8"path", asset != nullptr ? JsonValue::MakeString(asset->Path()) : JsonValue::MakeNull());
        return entry;
    }

    /// The settings as project_info reports them: an asset setting {guid, path} (null when unset),
    /// a list of them as an array, a string as text, a number as a number.
    inline JsonValue ProjectSettingsJson(editor::EditorProject& project)
    {
        engine::project::ProjectSettings& settings = project.Settings();
        const Instance instance(&settings, &engine::project::ProjectSettings::StaticType());
        JsonValue out = JsonValue::MakeObject();
        for (const PropertyInfo* property : ProjectSettingProperties())
        {
            const String key(PropertyName(*property));
            void* address = property->address(instance);
            if (engine::project::IsAssetListSetting(*property))
            {
                JsonValue list = JsonValue::MakeArray();
                for (const Guid& id : *static_cast<const Array<Guid>*>(address))
                {
                    list.Add(SettingAssetJson(project, id));
                }
                out.Set(key, Move(list));
            }
            else if (engine::project::IsAssetSetting(*property))
            {
                const Guid& id = *static_cast<const Guid*>(address);
                out.Set(key, id.IsNil() ? JsonValue::MakeNull() : SettingAssetJson(project, id));
            }
            else if (property->type == &TypeOf<String>())
            {
                out.Set(key, JsonValue::MakeString(*static_cast<const String*>(address)));
            }
            else if (property->type == &TypeOf<bool>())
            {
                out.Set(key, JsonValue::MakeBool(*static_cast<const bool*>(address)));
            }
            else if (IsEnum(*property->type))
            {
                const char* name = EnumValueName(*property->type, ReadEnumValue(address, *property->type));
                out.Set(key, name != nullptr
                                 ? JsonValue::MakeString(String(StringView(reinterpret_cast<const utf8char*>(name))))
                                 : JsonValue::MakeNull());
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
            const String* assetType =
                engine::project::SettingAttribute(*property, engine::project::kSettingAssetTypeAttribute);
            if (engine::project::IsAssetListSetting(*property))
            {
                schema.Arr(key, u8"string",
                           Format(u8"{}: the guids of assets of type {}, in order; the whole list, [] "
                                  u8"for none",
                                  label->AsView(), assetType->AsView()));
            }
            else if (assetType != nullptr)
            {
                const String* emptyText = engine::project::SettingAttribute(
                    *property, engine::project::kSettingEmptyTextAttribute);
                schema.Str(key, Format(u8"{}: the guid of an asset of type {}; \"\" clears it ({})",
                                       label->AsView(),
                                       assetType->AsView(),
                                       emptyText != nullptr ? emptyText->AsView() : StringView()));
            }
            else if (property->type == &TypeOf<String>())
            {
                schema.Str(key, *label);
            }
            else if (property->type == &TypeOf<bool>())
            {
                schema.Boolean(key, *label);
            }
            else if (IsEnum(*property->type))
            {
                Array<String> names;
                for (const EnumValue& value : Enumerators(*property->type))
                {
                    names.PushBack(String(StringView(reinterpret_cast<const utf8char*>(value.name))));
                }
                schema.Enum(key, Move(names), *label);
            }
            else
            {
                u32 least = 0;
                u32 most = 0;
                CountRange(*property, least, most);
                schema.Integer(key, most == 0xFFFFFFFFu ? *label : Format(u8"{}: {} to {}", label->AsView(), least, most));
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
            u8"type (the refusal says which), \"\" clears it; a list setting (uiFontIds, the fonts "
            u8"the game UI loads beside the default, each a family a label picks with font-family) "
            u8"takes the whole list, [] for none; a count takes its range (the display's sizes), a "
            u8"choice one of its values by name (renderFit, windowMode), a flag true or false; MSAA "
            u8"takes the render levels. Checked "
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
                    Array<Guid> ids; // a list setting's whole list
                    bool flag = false;
                    i64 choice = 0;
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
                    const String* assetType = engine::project::SettingAttribute(
                        *property, engine::project::kSettingAssetTypeAttribute);
                    // One guid naming an asset of the setting's type: the refusal, empty when it
                    // does; `where` names the setting in it.
                    const auto checkAsset = [&project, assetType](StringView where, StringView text,
                                                                  Guid& id) -> String
                    {
                        if (!Guid::TryParse(text, id))
                        {
                            return Format(u8"`{}`: '{}' is not a valid guid", where, text);
                        }
                        content::Instance* asset = project.SourceDb().GetInstance(id);
                        if (asset == nullptr)
                        {
                            return Format(u8"`{}`: no asset with guid {} in the project", where, text);
                        }
                        if (asset->TypeName() != assetType->AsView())
                        {
                            return Format(u8"`{}` takes an asset of type {}; '{}' is of type {}", where,
                                          assetType->AsView(), asset->Name(), asset->TypeName());
                        }
                        return String();
                    };
                    if (engine::project::IsAssetListSetting(*property))
                    {
                        if (!value.IsArray())
                        {
                            return Err(Format(u8"`{}` takes an array of {} guids", key.AsView(),
                                              assetType->AsView()));
                        }
                        for (i64 i = 0; i < value.Count(); ++i)
                        {
                            Guid id;
                            const String text = value.At(i).AsString();
                            String refused =
                                checkAsset(Format(u8"{}[{}]", key.AsView(), i).AsView(), text.AsView(), id);
                            if (!refused.IsEmpty())
                            {
                                return Err(Move(refused));
                            }
                            bool listed = false;
                            for (const Guid& other : change.ids)
                            {
                                listed = listed || other == id;
                            }
                            if (!listed)
                            {
                                change.ids.PushBack(id); // once each: a family loads once
                            }
                        }
                    }
                    else if (assetType != nullptr)
                    {
                        const String text = value.AsString();
                        if (!text.IsEmpty())
                        {
                            String refused = checkAsset(key.AsView(), text.AsView(), change.id);
                            if (!refused.IsEmpty())
                            {
                                return Err(Move(refused));
                            }
                        }
                    }
                    else if (property->type == &TypeOf<String>())
                    {
                        change.text = value.AsString();
                    }
                    else if (property->type == &TypeOf<bool>())
                    {
                        if (!value.IsBool())
                        {
                            return Err(Format(u8"`{}` takes true or false", key.AsView()));
                        }
                        change.flag = value.AsBool();
                    }
                    else if (IsEnum(*property->type))
                    {
                        const String name = value.AsString();
                        if (!EnumValueByName(*property->type, reinterpret_cast<const char*>(name.CStr()),
                                             change.choice))
                        {
                            return Err(Format(u8"`{}` takes {}", key.AsView(),
                                              detail::EnumNames(*property->type).AsView()));
                        }
                    }
                    else
                    {
                        u32 least = 0;
                        u32 most = 0;
                        detail::CountRange(*property, least, most);
                        if (!value.IsNumber() || value.AsNumber() < static_cast<f64>(least) ||
                            value.AsNumber() > static_cast<f64>(most))
                        {
                            return Err(most == 0xFFFFFFFFu
                                           ? Format(u8"`{}` takes a count from {}", key.AsView(), least)
                                           : Format(u8"`{}` takes {} to {}", key.AsView(), least, most));
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
                    if (engine::project::IsAssetListSetting(*change.property))
                    {
                        *static_cast<Array<Guid>*>(address) = change.ids;
                    }
                    else if (engine::project::IsAssetSetting(*change.property))
                    {
                        *static_cast<Guid*>(address) = change.id;
                    }
                    else if (change.property->type == &TypeOf<String>())
                    {
                        *static_cast<String*>(address) = change.text;
                    }
                    else if (change.property->type == &TypeOf<bool>())
                    {
                        *static_cast<bool*>(address) = change.flag;
                    }
                    else if (IsEnum(*change.property->type))
                    {
                        WriteEnumValue(address, *change.property->type, change.choice);
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
