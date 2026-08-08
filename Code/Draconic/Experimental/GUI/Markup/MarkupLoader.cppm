// Draconic GUI - :markup partition
//
// MarkupLoader: inflate a widget tree from XML markup. Modeled on eepp's layout loading (role
// only). An element name selects a widget from a WidgetFactory; nested elements become children;
// attributes are applied in three tiers:
//   - `id` / `class` set the widget's CSS identity,
//   - an attribute a widget claims (via UINode::SetMarkupAttribute - e.g. Label "text",
//     LinearLayout "orientation") is consumed structurally,
//   - everything else is treated as an inline CSS property and run through the StyleApplier,
// so the whole CSS property vocabulary (width/height/padding/margin/background-color/border/
// text-align/units/...) works in markup for free.
//
// Example:
//   <LinearLayout orientation="vertical" spacing="8" padding="12" class="panel">
//     <Label text="Hello" text-align="center"/>
//     <Button text="OK" width="120" class="accent"/>
//   </LinearLayout>

module;
#include "Core/Prelude.h"

export module draconic.gui:markup;

import draconic.core; // RefPtr, MakeRef, Array, String, StringView, Function, Move, Cast, IsWhiteSpace
import draconic.fonts; // IFontService
import draconic.xml;   // XmlDocument, XmlElement, XmlAttribute, XmlResult
import :node;
import :ui_node;
import :ui_widget;
import :style_sheet;   // ResolvedStyle
import :style_applier; // ApplyStyle
import :css_values;    // LengthContext
import :resource_provider;
// Widget partitions registered by the default factory:
import :label;
import :button;
import :check_box;
import :radio;
import :slider;
import :progress_bar;
import :text_field;
import :linear_layout;
import :grid_layout;
import :relative_layout;
import :flex_layout;
import :scroll_view;
import :image;
import :list_box;
import :window;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;
namespace xml = draconic::xml;

export namespace draconic::gui
{
    // Maps markup element names to widget constructors.
    class WidgetFactory
    {
    public:
        using Factory = core::Function<RefPtr<Node>()>;

        void Register(core::StringView name, Factory factory)
        {
            m_entries.PushBack(Entry{core::String(name), core::Move(factory)});
        }
        [[nodiscard]] bool Has(core::StringView name) const
        {
            for (const Entry& e : m_entries)
                if (e.Name.AsView() == name)
                    return true;
            return false;
        }
        [[nodiscard]] RefPtr<Node> Create(core::StringView name) const
        {
            for (const Entry& e : m_entries)
                if (e.Name.AsView() == name)
                    return e.Make ? e.Make() : RefPtr<Node>{};
            return RefPtr<Node>{};
        }

    private:
        struct Entry
        {
            core::String Name;
            Factory Make;
        };
        Array<Entry> m_entries;
    };

    // A factory pre-registered with the common built-in widgets (element name = class name).
    [[nodiscard]] inline WidgetFactory DefaultWidgetFactory()
    {
        WidgetFactory f;
        auto reg = [&f](core::StringView name, auto maker)
        {
            f.Register(name, [maker]() -> RefPtr<Node> { return maker(); });
        };
        reg(core::StringView(u8"Label"),
            [] { return core::MakeRef<Label>(core::DefaultAllocator()); });
        reg(core::StringView(u8"Button"),
            [] { return core::MakeRef<Button>(core::DefaultAllocator()); });
        reg(core::StringView(u8"CheckBox"),
            [] { return core::MakeRef<CheckBox>(core::DefaultAllocator()); });
        reg(core::StringView(u8"RadioButton"),
            [] { return core::MakeRef<RadioButton>(core::DefaultAllocator()); });
        reg(core::StringView(u8"Slider"),
            [] { return core::MakeRef<Slider>(core::DefaultAllocator()); });
        reg(core::StringView(u8"ProgressBar"),
            [] { return core::MakeRef<ProgressBar>(core::DefaultAllocator()); });
        reg(core::StringView(u8"TextField"),
            [] { return core::MakeRef<TextField>(core::DefaultAllocator()); });
        reg(core::StringView(u8"LinearLayout"),
            [] { return core::MakeRef<LinearLayout>(core::DefaultAllocator()); });
        reg(core::StringView(u8"GridLayout"),
            [] { return core::MakeRef<GridLayout>(core::DefaultAllocator()); });
        reg(core::StringView(u8"RelativeLayout"),
            [] { return core::MakeRef<RelativeLayout>(core::DefaultAllocator()); });
        reg(core::StringView(u8"FlexLayout"),
            [] { return core::MakeRef<FlexLayout>(core::DefaultAllocator()); });
        reg(core::StringView(u8"ScrollView"),
            [] { return core::MakeRef<ScrollView>(core::DefaultAllocator()); });
        reg(core::StringView(u8"Image"),
            [] { return core::MakeRef<Image>(core::DefaultAllocator()); });
        reg(core::StringView(u8"ListBox"),
            [] { return core::MakeRef<ListBox>(core::DefaultAllocator()); });
        reg(core::StringView(u8"Window"),
            [] { return core::MakeRef<Window>(core::DefaultAllocator()); });
        return f;
    }

    class MarkupLoader
    {
    public:
        explicit MarkupLoader(const WidgetFactory& factory, IResourceProvider* resources = nullptr,
                              fonts::IFontService* fontService = nullptr,
                              LengthContext lengths = {}) noexcept
            : m_factory(&factory), m_resources(resources), m_fontService(fontService),
              m_lengths(lengths)
        {
        }

        // Inflate the root element of `markup` into a widget tree; null on a parse error or an
        // unknown root element.
        [[nodiscard]] RefPtr<Node> LoadFromString(core::StringView markup)
        {
            xml::XmlDocument document;
            if (document.Parse(markup) != xml::XmlResult::Ok)
                return {};
            xml::XmlElement* root = FirstElementChild(document);
            return root != nullptr ? Inflate(*root) : RefPtr<Node>{};
        }

    private:
        [[nodiscard]] RefPtr<Node> Inflate(xml::XmlElement& element)
        {
            RefPtr<Node> widget = m_factory->Create(element.TagName());
            if (!widget)
                return {}; // unknown element name

            UINode* uinode = core::Cast<UINode>(widget.Get());
            UIWidget* uiwidget = core::Cast<UIWidget>(widget.Get());

            ResolvedStyle inlineStyle;
            for (xml::XmlAttribute* attr : element.Attributes())
            {
                const core::StringView name = attr->Name();
                const core::StringView value = attr->Value();
                if (name == core::StringView(u8"id"))
                {
                    if (uiwidget != nullptr)
                        uiwidget->SetId(value);
                    continue;
                }
                if (name == core::StringView(u8"class"))
                {
                    AddClasses(uiwidget, value);
                    continue;
                }
                if (uinode != nullptr && uinode->SetMarkupAttribute(name, value))
                    continue;
                inlineStyle.Set(name, value); // an inline CSS property
            }
            if (uinode != nullptr)
                ApplyStyle(*uinode, inlineStyle, m_resources, m_fontService, m_lengths);

            for (xml::XmlElement* child = element.FirstChildElement(); child != nullptr;
                 child = child->NextSiblingElement())
                if (RefPtr<Node> childWidget = Inflate(*child))
                    widget->AddChild(childWidget.Get());

            return widget;
        }

        static void AddClasses(UIWidget* widget, core::StringView classes)
        {
            if (widget == nullptr)
                return;
            usize start = 0;
            for (usize i = 0; i <= classes.Size(); ++i)
            {
                const bool boundary = (i == classes.Size()) || core::IsWhiteSpace(classes[i]);
                if (boundary)
                {
                    if (i > start)
                        widget->AddClass(classes.SubStr(start, i - start));
                    start = i + 1;
                }
            }
        }

        [[nodiscard]] static xml::XmlElement* FirstElementChild(xml::XmlNode& node)
        {
            for (xml::XmlNode* c = node.FirstChild(); c != nullptr; c = c->NextSibling())
                if (c->NodeType() == xml::XmlNodeType::Element)
                    return static_cast<xml::XmlElement*>(c);
            return nullptr;
        }

        const WidgetFactory* m_factory;
        IResourceProvider* m_resources;
        fonts::IFontService* m_fontService;
        LengthContext m_lengths;
    };
}
