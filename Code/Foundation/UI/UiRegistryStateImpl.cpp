// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Registry storage for the UI's markup/theme/type/drawable registries. Defined
// here - one instance per process - because registration and lookup happen in
// different libraries; inline function-local statics in the interfaces would
// duplicate per shared library (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.ui;

using namespace foundation::core;

namespace foundation::ui
{
    HashMap<String, MarkupRegistry::ViewFactory>& MarkupRegistry::ViewFactories()
    {
        static HashMap<String, ViewFactory> v;
        return v;
    }
    HashMap<String, MarkupRegistry::PropertySetter>& MarkupRegistry::ViewProps()
    {
        static HashMap<String, PropertySetter> v;
        return v;
    }
} // namespace foundation::ui

namespace foundation::ui
{
    ThemeIconSet& ThemeIconSet::Get()
    {
        static ThemeIconSet instance;
        return instance;
    }
} // namespace foundation::ui

namespace foundation::ui::detail
{
    Array<IThemeExtension*>& ThemeExtensionList()
    {
        static Array<IThemeExtension*> extensions;
        return extensions;
    }

    HashMap<String, const TypeInfo*>& UITypeMap()
    {
        static HashMap<String, const TypeInfo*> types;
        return types;
    }

    HashMap<String, DrawableFactoryRegistry::FactoryFn>& FactoryMap()
    {
        static HashMap<String, DrawableFactoryRegistry::FactoryFn> factories;
        return factories;
    }

    bool& DrawableBuiltinsRegisteredFlag()
    {
        static bool registered = false;
        return registered;
    }
} // namespace foundation::ui::detail
