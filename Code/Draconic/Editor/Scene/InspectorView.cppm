// Draconic::EditorScene - :inspector partition.
//
// SceneInspectorView: the reflection-driven property inspector INSIDE a scene page (per-page,
// like everything scene-scoped; §3.5). A toolkit PropertyGrid rebuilt from the primary
// selection: an Entity section (name / active), a Transform section (position / rotation-as-
// euler-degrees / scale), and one category per component with rows auto-generated from the
// component type's reflected properties (f32, ints, bool, String, Float3, Color, enums via the
// PropertyInfo::address raw path). Every edit routes through the SceneEditContext commands, so
// field scrubs merge into single undo entries. [+ Add Component] lists the scene's managers;
// each component category ends with a Remove row.
//
// Rebuild vs refresh: a cheap per-frame SIGNATURE (selected entity + scene revision + which
// managers have a component) decides structural rebuilds; otherwise per-editor refreshers pull
// model values into the widgets (skipped while that editor has an active edit gesture), so
// undo/redo and external changes (gizmos later) stay live.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include <limits>
#include <initializer_list>

export module draconic.editor.scene:inspector;

import draconic.core;
import draconic.content;
import draconic.resource;
import draconic.geometry;
import draconic.animation;
import draconic.materials;
import draconic.texture.resource;
import draconic.particles.resource;
import draconic.scene;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import draconic.editor.app;
import :edit;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::editor
{
    namespace ui = draconic::ui;
    namespace tk = draconic::ui::toolkit;
    namespace dscene = draconic::scene;

    // A property row for resource::Ref fields: [name | button showing the current asset,
    // click = picker menu]. The value text refreshes from the ref's Guid each frame.
    class ResourceRefEditor final : public tk::PropertyEditor
    {
        DRACONIC_OBJECT(ResourceRefEditor, tk::PropertyEditor)
    public:
        Function<void()> OnPick;   // opens the picker (wired by the inspector)

        ResourceRefEditor(StringView name, StringView valueText, StringView category)
            : tk::PropertyEditor(name, category), m_valueText(valueText) {}

        void SetValueText(StringView text)
        {
            if (m_valueText.AsView() == text) { return; }
            m_valueText = String(text);
            if (m_button.Get() != nullptr) { m_button->SetText(m_valueText.AsView()); }
        }

        void RefreshView() override {}

    protected:
        RefPtr<ui::View> CreateEditorView() override
        {
            m_button = MakeRef<ui::Button>(DefaultAllocator(), m_valueText.AsView());
            ResourceRefEditor* self = this;
            m_button->OnClick.Add([self](ui::ButtonBase*) { if (self->OnPick) { self->OnPick(); } });
            return RefPtr<ui::View>(m_button.Get());
        }

    private:
        String m_valueText;
        RefPtr<ui::Button> m_button;
    };


    // --- Property-attribute conventions (reflection PropAttribute metadata -> inspector) ---
    //
    //   "displayName"  String  - row label override (default: prettified property name)
    //   "description"  String  - row tooltip
    //   "range"        Float4  - {min, max, step, unused}: f32 rows become slider+field
    //   "visibleWhen"  String  - "prop" (visible while prop is truthy) or "prop=1,2"
    //                            (visible while prop's raw int value is in the list)

    /// Parsed "visibleWhen" condition.
    struct PropertyCondition
    {
        String prop;        // the dependent property's reflected name
        Array<i64> values;  // empty = truthy test
    };

    [[nodiscard]] inline bool ParsePropertyCondition(StringView spec, PropertyCondition& out)
    {
        const utf8char* d = spec.Data();
        usize eq = spec.Size();
        for (usize i = 0; i < spec.Size(); ++i) { if (d[i] == u8'=') { eq = i; break; } }
        if (eq == 0) { return false; }
        out.prop = String(spec.SubStr(0, eq));
        out.values.Clear();
        if (eq == spec.Size()) { return true; }   // truthy form
        i64 value = 0;
        bool negative = false;
        bool any = false;
        for (usize i = eq + 1; i <= spec.Size(); ++i)
        {
            const utf8char c = (i < spec.Size()) ? d[i] : u8',';   // sentinel comma flushes
            if (c == u8',')
            {
                if (!any) { return false; }
                out.values.PushBack(negative ? -value : value);
                value = 0; negative = false; any = false;
            }
            else if (c == u8'-' && !any && !negative) { negative = true; }
            else if (c >= u8'0' && c <= u8'9') { value = value * 10 + (c - u8'0'); any = true; }
            else { return false; }
        }
        return !out.values.IsEmpty();
    }

    [[nodiscard]] inline bool MatchesPropertyCondition(const PropertyCondition& condition, i64 raw)
    {
        if (condition.values.IsEmpty()) { return raw != 0; }
        for (i64 v : condition.values) { if (v == raw) { return true; } }
        return false;
    }

    /// "castsShadows" -> "Casts Shadows", "fovYRadians" -> "Fov Y Radians", "IBL" -> "IBL".
    [[nodiscard]] inline String PrettifyPropertyName(StringView name)
    {
        const utf8char* d = name.Data();
        String out;
        bool prevLower = false;
        bool prevUpper = false;
        for (usize i = 0; i < name.Size(); ++i)
        {
            utf8char c = d[i];
            const bool upper = (c >= u8'A' && c <= u8'Z');
            const bool lower = (c >= u8'a' && c <= u8'z');
            if (i == 0 && lower) { c = static_cast<utf8char>(c - (u8'a' - u8'A')); }
            else if (upper)
            {
                const bool nextLower = (i + 1 < name.Size())
                    && (d[i + 1] >= u8'a' && d[i + 1] <= u8'z');
                if (prevLower || (prevUpper && nextLower)) { out += u8' '; }
            }
            out += c;
            prevLower = lower;
            prevUpper = upper;
        }
        return out;
    }

    class SceneInspectorView : public ui::ViewGroup
    {
        DRACONIC_OBJECT(SceneInspectorView, ui::ViewGroup)
    public:
        SceneInspectorView(EditorContext& editor, SceneEditContext& edit)
            : m_editor(&editor), m_edit(&edit)
        {
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Padding = ui::Thickness{ 8, 6 };   // inset the content off the panel edge (like the hierarchy)

            m_grid = MakeRef<tk::PropertyGrid>(DefaultAllocator());
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                column->AddView(m_grid.Get(), grow);
            }

            m_addButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Add Component"));
            {
                SceneInspectorView* self = this;
                m_addButton->OnClick.Add([self](ui::ButtonBase*) { self->ShowAddComponentMenu(); });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_addButton.Get(), lp);
            }

            AddView(column.Get());
        }

        /// Per-frame: structural rebuild when the shape changed, else pull values into widgets.
        void Refresh()
        {
            const u64 signature = Signature();
            if (signature != m_signature)
            {
                m_signature = signature;
                Rebuild();
            }
            else
            {
                for (const Function<void()>& refresher : m_refreshers) { refresher(); }
            }
        }

        [[nodiscard]] tk::PropertyGrid* Grid() const noexcept { return m_grid.Get(); }

        // Fill the available space (wrap-to-children would collapse the scrolling grid).
        void OnMeasure(ui::BoxConstraints constraints) override
        {
            for (usize i = 0; i < ChildCount(); ++i) { GetChildAt(i)->Measure(constraints); }
            MeasuredSize = Float2{ constraints.MaxWidth, constraints.MaxHeight };
        }
        void OnLayout(f32, f32, f32 width, f32 height) override
        {
            for (usize i = 0; i < ChildCount(); ++i) { GetChildAt(i)->Layout(0, 0, width, height); }
        }

    private:
        // TypeOf<T> for a type nobody registered still exists, named "<value>" - such
        // components (e.g. from a game module without reflection) can't be edited or even
        // sensibly LISTED, so the Add menu skips them and sections fall back to the manager's
        // serialization id when available.
        [[nodiscard]] static bool IsRegisteredType(const TypeInfo* type)
        {
            if (type == nullptr || type->name == nullptr) { return false; }
            const char* n = type->name;
            return !(n[0] == '<');
        }

        [[nodiscard]] Guid SelectedEntity() const
        {
            const Guid* primary = m_edit->EntitySelection().Primary();
            return (primary != nullptr) ? *primary : Guid{};
        }

        // Selected entity + scene revision + which managers have a component on it.
        [[nodiscard]] u64 Signature()
        {
            const Guid id = SelectedEntity();
            u64 signature = id.high ^ (id.low * 0x9E3779B97F4A7C15ull) ^ m_edit->Scene().Revision();
            const dscene::EntityHandle e = m_edit->Resolve(id);
            if (e.IsAssigned())
            {
                u64 bit = 1;
                m_edit->Scene().ForEachManager([&](dscene::ComponentManagerBase& mgr) {
                    if (mgr.HasComponent(e)) { signature ^= bit * 0xBF58476D1CE4E5B9ull; }
                    bit <<= 1;
                });
            }
            return signature;
        }

        void Rebuild()
        {
            m_grid->Clear();
            m_refreshers.Clear();

            const Guid id = SelectedEntity();
            const dscene::EntityHandle e = m_edit->Resolve(id);
            m_addButton->Visibility = e.IsAssigned() ? ui::VisibilityValue::Visible
                                                     : ui::VisibilityValue::Gone;
            // No entity selected: the SCENE's settings (Sedulous scene-modules pattern) -
            // every scene system exposing a reflected settings block gets a category.
            if (!e.IsAssigned()) { BuildSceneSettingsSections(); Invalidate(); return; }

            BuildEntitySection(id);
            BuildTransformSection(id);

            m_edit->Scene().ForEachManager([&](dscene::ComponentManagerBase& mgr) {
                const dscene::EntityHandle live = m_edit->Resolve(id);
                if (live.IsAssigned() && mgr.HasComponent(live)) { BuildComponentSection(id, mgr); }
            });
            Invalidate();
        }

        void BuildEntitySection(const Guid& id)
        {
            SceneEditContext* edit = m_edit;

            auto name = MakeRef<tk::StringEditor>(DefaultAllocator(),
                StringView(u8"Name"), m_edit->Scene().GetEntityName(m_edit->Resolve(id)),
                Function<void(StringView)>{ [edit, id](StringView v) { edit->RenameEntity(id, v); } },
                StringView(u8"Entity"));
            AddEditor(name.Get(), [edit, id, raw = name.Get()]() {
                raw->SetValue(edit->Scene().GetEntityName(edit->Resolve(id)));
            });

            auto active = MakeRef<tk::BoolEditor>(DefaultAllocator(),
                StringView(u8"Active"), m_edit->Scene().IsActive(m_edit->Resolve(id)),
                Function<void(bool)>{ [edit, id](bool v) { edit->SetEntityActive(id, v); } },
                StringView(u8"Entity"));
            AddEditor(active.Get(), [edit, id, raw = active.Get()]() {
                raw->SetValue(edit->Scene().IsActive(edit->Resolve(id)));
            });
        }

        void BuildTransformSection(const Guid& id)
        {
            SceneEditContext* edit = m_edit;
            const StringView category = u8"Transform";
            const core::Transform t = m_edit->Scene().GetLocalTransform(m_edit->Resolve(id));

            auto position = MakeRef<tk::Float3Editor>(DefaultAllocator(),
                StringView(u8"Position"), t.position, -100000.0f, 100000.0f, 0.1f,
                Function<void(Float3)>{ [edit, id](Float3 v) {
                    core::Transform current = edit->Scene().GetLocalTransform(edit->Resolve(id));
                    current.position = v;
                    edit->SetLocalTransform(id, current);
                } }, category);
            AddEditor(position.Get(), [edit, id, raw = position.Get()]() {
                raw->SetValue(edit->Scene().GetLocalTransform(edit->Resolve(id)).position);
            });

            // Rotation displayed as euler DEGREES (x = pitch, y = yaw, z = roll).
            auto rotation = MakeRef<tk::Float3Editor>(DefaultAllocator(),
                StringView(u8"Rotation"), EulerDegrees(t.rotation), -360.0f, 360.0f, 1.0f,
                Function<void(Float3)>{ [edit, id](Float3 v) {
                    core::Transform current = edit->Scene().GetLocalTransform(edit->Resolve(id));
                    current.rotation = FromYawPitchRoll(DegreesToRadians(v.y),
                                                        DegreesToRadians(v.x),
                                                        DegreesToRadians(v.z));
                    edit->SetLocalTransform(id, current);
                } }, category);
            AddEditor(rotation.Get(), [edit, id, raw = rotation.Get()]() {
                raw->SetValue(EulerDegrees(edit->Scene().GetLocalTransform(edit->Resolve(id)).rotation));
            });

            auto scale = MakeRef<tk::Float3Editor>(DefaultAllocator(),
                StringView(u8"Scale"), t.scale, -100000.0f, 100000.0f, 0.1f,
                Function<void(Float3)>{ [edit, id](Float3 v) {
                    core::Transform current = edit->Scene().GetLocalTransform(edit->Resolve(id));
                    current.scale = v;
                    edit->SetLocalTransform(id, current);
                } }, category);
            AddEditor(scale.Get(), [edit, id, raw = scale.Get()]() {
                raw->SetValue(edit->Scene().GetLocalTransform(edit->Resolve(id)).scale);
            });
        }

        void BuildSceneSettingsSections()
        {
            m_edit->Scene().ForEachSystem([&](dscene::SceneSystem& system) {
                const TypeInfo* type = system.SettingsType();
                if (type == nullptr || !IsRegisteredType(type)) { return; }
                // Category = the settings type minus a trailing "Settings"
                // ("EnvironmentSettings" -> "Environment").
                StringView category(reinterpret_cast<const utf8char*>(type->name));
                const StringView suffix = u8"Settings";
                if (category.Size() > suffix.Size()
                    && category.SubStr(category.Size() - suffix.Size(), suffix.Size()) == suffix)
                {
                    category = category.SubStr(0, category.Size() - suffix.Size());
                }
                for (const PropertyInfo& prop : Properties(*type))
                {
                    const usize firstRow = m_grid->PropertyCount();
                    BuildSettingRow(type, prop, category);
                    ApplyPropertyPresentation(type, prop, firstRow,
                        [edit = m_edit, type]() -> Instance {
                            dscene::SceneSystem* system = edit->FindSystemBySettingsType(type);
                            return (system != nullptr)
                                ? Instance{ system->SettingsInstance(), type } : Instance{};
                        });
                }
            });
        }

        // A scene-setting property row: same editor kinds as components, but reading the
        // system's settings instance and writing through SetSceneSettingProperty commands
        // (merged scrubs, one undo entry). Covers the kinds settings blocks use today.
        void BuildSettingRow(const TypeInfo* type, const PropertyInfo& prop, StringView category)
        {
            SceneEditContext* edit = m_edit;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));
            const bool readOnly = (static_cast<u32>(prop.flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0;
            const char* propName = prop.name;

            auto getInstance = [edit, type]() -> Instance {
                dscene::SceneSystem* system = edit->FindSystemBySettingsType(type);
                return (system != nullptr) ? Instance{ system->SettingsInstance(), type } : Instance{};
            };

            // Resource references (the environment's sky texture): the browser-mirroring picker,
            // writing through the settings-flavored ref command.
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::texture::Texture>>())
            {
                BuildSettingResourceRefRow<draconic::texture::Texture>(type, prop, category,
                    { u8"TextureAsset" });
                return;
            }
            auto getVariant = [getInstance, type, propName]() -> Variant {
                const Instance settings = getInstance();
                const PropertyInfo* p = settings.IsEmpty() ? nullptr : FindProperty(*type, propName);
                return (p != nullptr) ? GetProperty(*p, settings) : Variant{};
            };

            if (IsEnum(*prop.type))
            {
                const Span<const EnumValue> values = Enumerators(*prop.type);
                Array<StringView> items;
                for (const EnumValue& v : values) { items.PushBack(StringView(reinterpret_cast<const utf8char*>(v.name))); }

                auto rawRead = [getInstance, type, propName]() -> i64 {
                    const Instance settings = getInstance();
                    const PropertyInfo* p = settings.IsEmpty() ? nullptr : FindProperty(*type, propName);
                    void* address = (p != nullptr && p->address != nullptr) ? p->address(settings) : nullptr;
                    if (address == nullptr) { return 0; }
                    switch (p->type->size)
                    {
                        case 1: return *static_cast<const i8*>(address);
                        case 2: return *static_cast<const i16*>(address);
                        case 8: return *static_cast<const i64*>(address);
                        default: return *static_cast<const i32*>(address);
                    }
                };
                auto indexOf = [values](i64 value) -> i32 {
                    for (usize i = 0; i < values.Size(); ++i)
                    {
                        if (values[i].value == value) { return static_cast<i32>(i); }
                    }
                    return 0;
                };
                auto editor = MakeRef<tk::EnumEditor>(DefaultAllocator(), name, indexOf(rawRead()),
                    Span<const StringView>{ items.Data(), items.Size() },
                    readOnly ? Function<void(i32)>{} : Function<void(i32)>{
                        [edit, type, propName, values](i32 index) {
                            if (index >= 0 && index < static_cast<i32>(values.Size()))
                            {
                                edit->SetSceneSettingPropertyRaw(type, propName, values[static_cast<usize>(index)].value);
                            }
                        } },
                    category);
                AddEditor(editor.Get(), [rawRead, indexOf, raw = editor.Get()]() {
                    raw->SetValue(indexOf(rawRead()));
                });
                return;
            }

            if (prop.type == &TypeOf<f32>())
            {
                auto value = [getVariant]() -> f64 {
                    const Variant v = getVariant();
                    const f32* f = v.TryGet<f32>();
                    return (f != nullptr) ? static_cast<f64>(*f) : 0.0;
                };
                // "range" attribute -> bounded slider+field instead of a bare numeric field.
                if (const Float4* range = RangeOf(prop))
                {
                    auto editor = MakeRef<tk::RangeEditor>(DefaultAllocator(), name,
                        static_cast<f32>(value()), range->x, range->y, range->z,
                        readOnly ? Function<void(f32)>{} : Function<void(f32)>{
                            [edit, type, propName](f32 v) {
                                edit->SetSceneSettingProperty(type, propName, Variant::From<f32>(v));
                            } },
                        category);
                    AddEditor(editor.Get(), [value, raw = editor.Get()]() {
                        raw->SetValue(static_cast<f32>(value()));
                    });
                    return;
                }
                auto editor = MakeRef<tk::FloatEditor>(DefaultAllocator(), name, value(),
                    -1e9, 1e9, 0.1, 2,
                    readOnly ? Function<void(f64)>{} : Function<void(f64)>{
                        [edit, type, propName](f64 v) {
                            edit->SetSceneSettingProperty(type, propName, Variant::From<f32>(static_cast<f32>(v)));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<Color>())
            {
                auto value = [getVariant]() -> Color {
                    const Variant v = getVariant();
                    const Color* c = v.TryGet<Color>();
                    return (c != nullptr) ? *c : Color{ 1, 1, 1, 1 };
                };
                auto editor = MakeRef<tk::ColorEditor>(DefaultAllocator(), name, value(),
                    readOnly ? Function<void(Color)>{} : Function<void(Color)>{
                        [edit, type, propName](Color v) {
                            edit->SetSceneSettingProperty(type, propName, Variant::From<Color>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<bool>())
            {
                auto value = [getVariant]() -> bool {
                    const Variant v = getVariant();
                    const bool* b = v.TryGet<bool>();
                    return (b != nullptr) && *b;
                };
                auto editor = MakeRef<tk::BoolEditor>(DefaultAllocator(), name, value(),
                    readOnly ? Function<void(bool)>{} : Function<void(bool)>{
                        [edit, type, propName](bool v) {
                            edit->SetSceneSettingProperty(type, propName, Variant::From<bool>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<Float3>())
            {
                auto value = [getVariant]() -> Float3 {
                    const Variant v = getVariant();
                    const Float3* f = v.TryGet<Float3>();
                    return (f != nullptr) ? *f : Float3{};
                };
                auto editor = MakeRef<tk::Float3Editor>(DefaultAllocator(), name, value(),
                    -100000.0f, 100000.0f, 0.1f,
                    readOnly ? Function<void(Float3)>{} : Function<void(Float3)>{
                        [edit, type, propName](Float3 v) {
                            edit->SetSceneSettingProperty(type, propName, Variant::From<Float3>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }
            // Other kinds: extend when a settings block needs them.
        }

        void BuildComponentSection(const Guid& id, dscene::ComponentManagerBase& mgr)
        {
            const TypeInfo* type = mgr.ComponentType();
            if (type == nullptr) { return; }
            // Category = the type name minus a trailing "Component", prettified
            // ("ReflectionProbeComponent" -> "Reflection Probe").
            const StringView fallback = mgr.SerializationTypeId();
            String categoryStorage;
            if (IsRegisteredType(type))
            {
                StringView n(reinterpret_cast<const utf8char*>(type->name));
                const StringView suffix = u8"Component";
                if (n.Size() > suffix.Size()
                    && n.SubStr(n.Size() - suffix.Size(), suffix.Size()) == suffix)
                {
                    n = n.SubStr(0, n.Size() - suffix.Size());
                }
                categoryStorage = PrettifyPropertyName(n);
            }
            else
            {
                categoryStorage = fallback.IsEmpty() ? StringView(u8"(unreflected component)") : fallback;
            }
            const StringView category = categoryStorage.AsView();

            for (const PropertyInfo& prop : Properties(*type))
            {
                const usize firstRow = m_grid->PropertyCount();
                BuildPropertyRow(id, type, prop, category);
                ApplyPropertyPresentation(type, prop, firstRow,
                    [edit = m_edit, id, type]() -> Instance {
                        dscene::ComponentManagerBase* mgr = edit->FindManager(type);
                        const dscene::EntityHandle e = edit->Resolve(id);
                        return (mgr != nullptr && e.IsAssigned())
                            ? mgr->GetComponentInstance(e) : Instance{};
                    });
            }

            SceneEditContext* edit = m_edit;
            EditorContext* editor = m_editor;

            // Prefab members: a per-component revert row whose label carries a LIVE override
            // dot (recomputed by the refresher, so it tracks edits and undo without grid
            // rebuilds). Revert rides the undoable paste-component path.
            {
                dscene::PrefabMemberInfo member;
                if (mgr.IsSerializable() && dscene::FindPrefabMember(edit->Scene(), id, member))
                {
                    auto revert = MakeRef<tk::ButtonEditor>(DefaultAllocator(),
                        StringView(u8"Revert to Prefab"),
                        Function<void()>{ [edit, id, type]() {
                            (void)edit->RevertComponentToBaseline(id, type);
                        } }, category);
                    revert->SetTooltip(u8"Reverts this component to the prefab's values (undoable).");
                    revert->SetButtonEnabled(false);   // refresher enables it on an override
                    dscene::ComponentManagerBase* manager = &mgr;
                    AddEditor(revert.Get(), [edit, id, manager, raw = revert.Get()]() {
                        dscene::PrefabMemberInfo m;
                        const bool overridden = dscene::FindPrefabMember(edit->Scene(), id, m)
                            && dscene::IsPrefabComponentOverridden(edit->Scene(), m, *manager);
                        raw->SetButtonEnabled(overridden);
                        // " *" matches the dirty-tab convention AND stays inside the editor
                        // font's rasterized range (ExtendedLatin = codepoints <= 255; a
                        // U+25CF dot has no glyph and silently renders as nothing).
                        raw->SetDisplayName(overridden ? StringView(u8"Revert to Prefab *")
                                                       : StringView(u8"Revert to Prefab"));
                    });
                }
            }

            auto copy = MakeRef<tk::ButtonEditor>(DefaultAllocator(), StringView(u8"Copy"),
                Function<void()>{ [edit, editor, id, type]() {
                    Array<byte> blob = edit->CopyComponent(id, type);
                    if (!blob.IsEmpty()) { editor->SetClipboard(u8"component", Move(blob)); }
                } }, category);
            m_grid->AddProperty(RefPtr<tk::PropertyEditor>(copy.Get()));
            auto remove = MakeRef<tk::ButtonEditor>(DefaultAllocator(), StringView(u8"Remove"),
                Function<void()>{ [edit, id, type]() { edit->RemoveComponent(id, type); } }, category);
            m_grid->AddProperty(RefPtr<tk::PropertyEditor>(remove.Get()));
        }

        // Raw integral value of a bool/enum/int property via the address escape hatch.
        [[nodiscard]] static i64 RawIntValue(const Instance& obj, const PropertyInfo& p)
        {
            void* address = (p.address != nullptr) ? p.address(obj) : nullptr;
            if (address == nullptr) { return 0; }
            switch (p.type->size)
            {
                case 1: return *static_cast<const i8*>(address);
                case 2: return *static_cast<const i16*>(address);
                case 8: return *static_cast<const i64*>(address);
                default: return *static_cast<const i32*>(address);
            }
        }

        // Applies the displayName/description/visibleWhen conventions to every row that
        // `prop`'s Build*Row call just added (rows firstRow..end). `instance` is a copyable
        // callable re-reading the owning object each frame so visibleWhen rows follow live
        // edits (a plain lambda, NOT core::Function - that one is move-only and each row's
        // refresher needs its own copy).
        template <typename GetInstance>
        void ApplyPropertyPresentation(const TypeInfo* type, const PropertyInfo& prop,
                                       usize firstRow, GetInstance instance)
        {
            const core::Attribute* displayName = FindAttribute(prop, u8"displayName");
            const core::Attribute* description = FindAttribute(prop, u8"description");
            const core::Attribute* visibleWhen = FindAttribute(prop, u8"visibleWhen");

            // Resolve the dependent property + condition once; refreshers share them.
            const PropertyInfo* dependent = nullptr;
            PropertyCondition condition;
            if (visibleWhen != nullptr)
            {
                const String* spec = visibleWhen->value.TryGet<String>();
                if (spec != nullptr && ParsePropertyCondition(spec->AsView(), condition))
                {
                    for (const PropertyInfo& p : Properties(*type))
                    {
                        if (StringView(reinterpret_cast<const utf8char*>(p.name)) == condition.prop.AsView())
                        {
                            dependent = &p;
                            break;
                        }
                    }
                }
            }

            for (usize i = firstRow; i < m_grid->PropertyCount(); ++i)
            {
                tk::PropertyEditor* editor = m_grid->PropertyAt(i);
                const String* label = (displayName != nullptr) ? displayName->value.TryGet<String>() : nullptr;
                editor->SetDisplayName(label != nullptr ? label->AsView()
                                                        : PrettifyPropertyName(editor->Name()).AsView());
                if (description != nullptr)
                {
                    if (const String* s = description->value.TryGet<String>()) { editor->SetTooltip(s->AsView()); }
                }
                if (dependent != nullptr)
                {
                    auto refresh = [editor, dependent, condition, get = instance]() {
                        const Instance obj = get();
                        editor->SetRowVisible(!obj.IsEmpty()
                            && MatchesPropertyCondition(condition, RawIntValue(obj, *dependent)));
                    };
                    refresh();
                    m_refreshers.PushBack(Function<void()>{ Move(refresh) });
                }
            }
        }

        void BuildPropertyRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                              StringView category)
        {
            SceneEditContext* edit = m_edit;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));
            const bool readOnly = (static_cast<u32>(prop.flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0;
            const char* propName = prop.name;

            // Resource references: a picker over the source DB's matching assets. Matched by
            // EXACT Ref<T> type identity (the TypeInfo pointer), so the unregistered template
            // type name ("<value>") never matters.
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::geometry::StaticMesh>>())
            {
                // SkinnedMeshAsset too: SkinnedMesh IS-A StaticMesh (bind pose when drawn
                // through the static path), so both asset types are valid targets.
                BuildResourceRefRow<draconic::geometry::StaticMesh>(id, type, prop, category,
                    { u8"StaticMeshAsset", u8"SkinnedMeshAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::materials::Material>>())
            {
                BuildResourceRefRow<draconic::materials::Material>(id, type, prop, category,
                    { u8"MaterialAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::animation::Skeleton>>())
            {
                BuildResourceRefRow<draconic::animation::Skeleton>(id, type, prop, category,
                    { u8"SkeletonAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::animation::AnimationClip>>())
            {
                BuildResourceRefRow<draconic::animation::AnimationClip>(id, type, prop, category,
                    { u8"AnimationClipAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::animation::AnimationGraph>>())
            {
                BuildResourceRefRow<draconic::animation::AnimationGraph>(id, type, prop, category,
                    { u8"AnimationGraphAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::texture::Texture>>())
            {
                BuildResourceRefRow<draconic::texture::Texture>(id, type, prop, category,
                    { u8"TextureAsset" });
                return;
            }
            if (prop.type == &TypeOf<draconic::resource::Ref<draconic::particles::ParticleEffectResource>>())
            {
                BuildResourceRefRow<draconic::particles::ParticleEffectResource>(id, type, prop, category,
                    { u8"ParticleEffectAsset" });
                return;
            }

            // Pulls the current Variant (empty component -> default Variant guards below).
            auto getVariant = [edit, id, type, propName]() -> Variant {
                dscene::ComponentManagerBase* mgr = edit->FindManager(type);
                const dscene::EntityHandle e = edit->Resolve(id);
                if (mgr == nullptr || !e.IsAssigned()) { return {}; }
                const Instance component = mgr->GetComponentInstance(e);
                const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
                return (p != nullptr) ? GetProperty(*p, component) : Variant{};
            };

            if (IsEnum(*prop.type))
            {
                const Span<const EnumValue> values = Enumerators(*prop.type);
                Array<StringView> items;
                for (const EnumValue& v : values) { items.PushBack(StringView(reinterpret_cast<const utf8char*>(v.name))); }

                auto rawRead = [edit, id, type, propName]() -> i64 {
                    dscene::ComponentManagerBase* mgr = edit->FindManager(type);
                    const dscene::EntityHandle e = edit->Resolve(id);
                    if (mgr == nullptr || !e.IsAssigned()) { return 0; }
                    const Instance component = mgr->GetComponentInstance(e);
                    const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
                    void* address = (p != nullptr && p->address != nullptr) ? p->address(component) : nullptr;
                    if (address == nullptr) { return 0; }
                    switch (p->type->size)
                    {
                        case 1: return *static_cast<const i8*>(address);
                        case 2: return *static_cast<const i16*>(address);
                        case 8: return *static_cast<const i64*>(address);
                        default: return *static_cast<const i32*>(address);
                    }
                };
                auto indexOf = [values](i64 value) -> i32 {
                    for (usize i = 0; i < values.Size(); ++i)
                    {
                        if (values[i].value == value) { return static_cast<i32>(i); }
                    }
                    return 0;
                };

                auto editor = MakeRef<tk::EnumEditor>(DefaultAllocator(), name, indexOf(rawRead()),
                    Span<const StringView>{ items.Data(), items.Size() },
                    readOnly ? Function<void(i32)>{} : Function<void(i32)>{
                        [edit, id, type, propName, values](i32 index) {
                            if (index >= 0 && index < static_cast<i32>(values.Size()))
                            {
                                edit->SetComponentPropertyRaw(id, type, propName, values[static_cast<usize>(index)].value);
                            }
                        } },
                    category);
                AddEditor(editor.Get(), [rawRead, indexOf, raw = editor.Get()]() {
                    raw->SetValue(indexOf(rawRead()));
                });
                return;
            }

            if (prop.type == &TypeOf<f32>())
            {
                auto value = [getVariant]() -> f64 {
                    const Variant v = getVariant();
                    const f32* f = v.TryGet<f32>();
                    return (f != nullptr) ? static_cast<f64>(*f) : 0.0;
                };
                // "range" attribute -> bounded slider+field instead of a bare numeric field.
                if (const Float4* range = RangeOf(prop))
                {
                    auto editor = MakeRef<tk::RangeEditor>(DefaultAllocator(), name,
                        static_cast<f32>(value()), range->x, range->y, range->z,
                        readOnly ? Function<void(f32)>{} : Function<void(f32)>{
                            [edit, id, type, propName](f32 v) {
                                edit->SetComponentProperty(id, type, propName, Variant::From<f32>(v));
                            } },
                        category);
                    AddEditor(editor.Get(), [value, raw = editor.Get()]() {
                        raw->SetValue(static_cast<f32>(value()));
                    });
                    return;
                }
                auto editor = MakeRef<tk::FloatEditor>(DefaultAllocator(), name, value(),
                    -1e9, 1e9, 0.1, 2,
                    readOnly ? Function<void(f64)>{} : Function<void(f64)>{
                        [edit, id, type, propName](f64 v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<f32>(static_cast<f32>(v)));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<bool>())
            {
                auto value = [getVariant]() -> bool {
                    const Variant v = getVariant();
                    const bool* b = v.TryGet<bool>();
                    return b != nullptr && *b;
                };
                auto editor = MakeRef<tk::BoolEditor>(DefaultAllocator(), name, value(),
                    readOnly ? Function<void(bool)>{} : Function<void(bool)>{
                        [edit, id, type, propName](bool v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<bool>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<i32>() || prop.type == &TypeOf<u32>()
                || prop.type == &TypeOf<i64>() || prop.type == &TypeOf<u64>())
            {
                const TypeInfo* intType = prop.type;
                auto value = [getVariant, intType]() -> i64 {
                    const Variant v = getVariant();
                    if (intType == &TypeOf<i32>()) { const i32* p = v.TryGet<i32>(); return p ? *p : 0; }
                    if (intType == &TypeOf<u32>()) { const u32* p = v.TryGet<u32>(); return p ? static_cast<i64>(*p) : 0; }
                    if (intType == &TypeOf<u64>()) { const u64* p = v.TryGet<u64>(); return p ? static_cast<i64>(*p) : 0; }
                    const i64* p = v.TryGet<i64>();
                    return p ? *p : 0;
                };
                auto setter = [edit, id, type, propName, intType](i64 v) {
                    if (intType == &TypeOf<i32>()) { edit->SetComponentProperty(id, type, propName, Variant::From<i32>(static_cast<i32>(v))); }
                    else if (intType == &TypeOf<u32>()) { edit->SetComponentProperty(id, type, propName, Variant::From<u32>(static_cast<u32>(v))); }
                    else if (intType == &TypeOf<u64>()) { edit->SetComponentProperty(id, type, propName, Variant::From<u64>(static_cast<u64>(v))); }
                    else { edit->SetComponentProperty(id, type, propName, Variant::From<i64>(v)); }
                };
                auto editor = MakeRef<tk::IntEditor>(DefaultAllocator(), name, value(),
                    std::numeric_limits<i64>::min(), std::numeric_limits<i64>::max(),
                    readOnly ? Function<void(i64)>{} : Function<void(i64)>{ Move(setter) },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<String>())
            {
                auto value = [getVariant]() -> String {
                    const Variant v = getVariant();
                    const String* s = v.TryGet<String>();
                    return (s != nullptr) ? String(*s) : String{};
                };
                auto editor = MakeRef<tk::StringEditor>(DefaultAllocator(), name, value().AsView(),
                    readOnly ? Function<void(StringView)>{} : Function<void(StringView)>{
                        [edit, id, type, propName](StringView v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<String>(String(v)));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value().AsView()); });
                return;
            }

            if (prop.type == &TypeOf<Float3>())
            {
                auto value = [getVariant]() -> Float3 {
                    const Variant v = getVariant();
                    const Float3* f = v.TryGet<Float3>();
                    return (f != nullptr) ? *f : Float3{};
                };
                auto editor = MakeRef<tk::Float3Editor>(DefaultAllocator(), name, value(),
                    -100000.0f, 100000.0f, 0.1f,
                    readOnly ? Function<void(Float3)>{} : Function<void(Float3)>{
                        [edit, id, type, propName](Float3 v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<Float3>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<Color>())
            {
                auto value = [getVariant]() -> Color {
                    const Variant v = getVariant();
                    const Color* c = v.TryGet<Color>();
                    return (c != nullptr) ? *c : Color{ 1, 1, 1, 1 };
                };
                auto editor = MakeRef<tk::ColorEditor>(DefaultAllocator(), name, value(),
                    readOnly ? Function<void(Color)>{} : Function<void(Color)>{
                        [edit, id, type, propName](Color v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<Color>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<Float2>())
            {
                auto value = [getVariant]() -> Float2 {
                    const Variant v = getVariant();
                    const Float2* f = v.TryGet<Float2>();
                    return (f != nullptr) ? *f : Float2{};
                };
                auto editor = MakeRef<tk::Float2Editor>(DefaultAllocator(), name, value(),
                    -100000.0f, 100000.0f, 0.1f,
                    readOnly ? Function<void(Float2)>{} : Function<void(Float2)>{
                        [edit, id, type, propName](Float2 v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<Float2>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            if (prop.type == &TypeOf<Float4>())
            {
                auto value = [getVariant]() -> Float4 {
                    const Variant v = getVariant();
                    const Float4* f = v.TryGet<Float4>();
                    return (f != nullptr) ? *f : Float4{};
                };
                auto editor = MakeRef<tk::Float4Editor>(DefaultAllocator(), name, value(),
                    -100000.0f, 100000.0f, 0.1f,
                    readOnly ? Function<void(Float4)>{} : Function<void(Float4)>{
                        [edit, id, type, propName](Float4 v) {
                            edit->SetComponentProperty(id, type, propName, Variant::From<Float4>(v));
                        } },
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
                return;
            }

            // Unsupported reflected type: skipped.
        }

        // Current target Guid of a Ref<T> property (nil when unset/unresolvable).
        template <typename T>
        [[nodiscard]] Guid RefTarget(const Guid& id, const TypeInfo* type, const char* propName)
        {
            dscene::ComponentManagerBase* mgr = m_edit->FindManager(type);
            const dscene::EntityHandle e = m_edit->Resolve(id);
            if (mgr == nullptr || !e.IsAssigned()) { return Guid{}; }
            const Instance component = mgr->GetComponentInstance(e);
            const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
            void* address = (p != nullptr && p->address != nullptr) ? p->address(component) : nullptr;
            return (address != nullptr) ? static_cast<draconic::resource::Ref<T>*>(address)->id : Guid{};
        }

        [[nodiscard]] StringView AssetNameFor(const Guid& target)
        {
            if (target.IsNil()) { return u8"(none)"; }
            if (m_editor->Project() != nullptr)
            {
                if (draconic::content::Instance* inst = m_editor->Project()->SourceDb().GetInstance(target))
                {
                    return inst->Name();
                }
            }
            return u8"(missing)";
        }

        // The settings twin of RefTarget (the Ref lives on a scene system's settings block).
        template <typename T>
        [[nodiscard]] Guid SettingRefTarget(const TypeInfo* type, const char* propName)
        {
            dscene::SceneSystem* system = m_edit->FindSystemBySettingsType(type);
            if (system == nullptr) { return Guid{}; }
            const Instance settings{ system->SettingsInstance(), type };
            const PropertyInfo* p = FindProperty(*type, propName);
            void* address = (p != nullptr && p->address != nullptr) ? p->address(settings) : nullptr;
            return (address != nullptr) ? static_cast<draconic::resource::Ref<T>*>(address)->id : Guid{};
        }

        // The settings twin of BuildResourceRefRow.
        template <typename T>
        void BuildSettingResourceRefRow(const TypeInfo* type, const PropertyInfo& prop,
                                        StringView category, std::initializer_list<StringView> assetTypeNames)
        {
            SceneInspectorView* self = this;
            SceneEditContext* edit = m_edit;
            const char* propName = prop.name;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));

            auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), name,
                AssetNameFor(SettingRefTarget<T>(type, propName)), category);
            ResourceRefEditor* raw = editor.Get();
            Array<String> assetTypes;
            for (StringView typeName : assetTypeNames) { assetTypes.PushBack(String(typeName)); }
            raw->OnPick = [self, edit, type, propName, assetTypes]() {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr) { return; }
                draconic::resource::ResourceManager* resources = self->m_editor->Resources();
                Array<String> typeNames = assetTypes;
                auto dialog = MakeRef<draconic::editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_editor, Move(typeNames));
                dialog->OnPicked = [edit, type, propName, resources](const Guid& target) {
                    edit->SetSceneSettingResourceRef<T>(type, propName, target, resources);
                };
                dialog->Show(self->Context);
            };
            AddEditor(raw, [self, type, propName, raw]() {
                raw->SetValueText(self->AssetNameFor(self->SettingRefTarget<T>(type, propName)));
            });
        }

        template <typename T>
        void BuildResourceRefRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                                 StringView category, std::initializer_list<StringView> assetTypeNames)
        {
            SceneInspectorView* self = this;
            SceneEditContext* edit = m_edit;
            const char* propName = prop.name;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));

            auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), name,
                AssetNameFor(RefTarget<T>(id, type, propName)), category);
            ResourceRefEditor* raw = editor.Get();
            Array<String> assetTypes;
            for (StringView typeName : assetTypeNames) { assetTypes.PushBack(String(typeName)); }
            raw->OnPick = [self, edit, id, type, propName, assetTypes]() {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr) { return; }
                draconic::resource::ResourceManager* resources = self->m_editor->Resources();

                // The browser-mirroring picker (readonly; favorites pinned first; [Clear] = none).
                Array<String> typeNames = assetTypes;
                auto dialog = MakeRef<draconic::editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_editor, Move(typeNames));
                dialog->OnPicked = [edit, id, type, propName, resources](const Guid& target) {
                    edit->SetComponentResourceRef<T>(id, type, propName, target, resources);
                };
                dialog->Show(self->Context);
            };
            AddEditor(raw, [self, id, type, propName, raw]() {
                raw->SetValueText(self->AssetNameFor(self->RefTarget<T>(id, type, propName)));
            });
        }

        // The "range" attribute's {min, max, step} payload, or null when absent/mistyped.
        [[nodiscard]] static const Float4* RangeOf(const PropertyInfo& prop)
        {
            const core::Attribute* attr = FindAttribute(prop, u8"range");
            return (attr != nullptr) ? attr->value.TryGet<Float4>() : nullptr;
        }

        void AddEditor(tk::PropertyEditor* editor, Function<void()> refresher)
        {
            m_grid->AddProperty(RefPtr<tk::PropertyEditor>(editor));
            tk::PropertyEditor* raw = editor;
            m_refreshers.PushBack(Function<void()>{
                [raw, pull = Move(refresher)]() {
                    if (!raw->IsEditing()) { pull(); }
                } });
        }

        [[nodiscard]] static Float3 EulerDegrees(Quaternion q)
        {
            f32 yaw = 0, pitch = 0, roll = 0;
            ToYawPitchRoll(q, yaw, pitch, roll);
            return Float3{ RadiansToDegrees(pitch), RadiansToDegrees(yaw), RadiansToDegrees(roll) };
        }

        void ShowAddComponentMenu()
        {
            const Guid id = SelectedEntity();
            const dscene::EntityHandle e = m_edit->Resolve(id);
            if (!e.IsAssigned() || Context == nullptr) { return; }

            SceneEditContext* edit = m_edit;
            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
            m_edit->Scene().ForEachManager([&](dscene::ComponentManagerBase& mgr) {
                const TypeInfo* type = mgr.ComponentType();
                if (!IsRegisteredType(type) || mgr.HasComponent(e)) { return; }   // skip unreflected
                menu->AddItem(StringView(reinterpret_cast<const utf8char*>(type->name)),
                              [edit, id, type]() { edit->AddComponent(id, type); });
            });
            // Paste a copied component (adds or overwrites; one undo step).
            const Span<const byte> clip = m_editor->ClipboardData(u8"component");
            if (!clip.IsEmpty())
            {
                String label(u8"Paste ");
                label += SceneEditContext::PeekComponentTypeId(clip);
                EditorContext* editor = m_editor;
                menu->AddSeparator();
                menu->AddItem(label.AsView(), [edit, editor, id]() {
                    (void)edit->PasteComponent(id, editor->ClipboardData(u8"component"));
                });
            }
            const Float2 screenPos = m_addButton->LocalToScreen(Float2{ 0.0f, 0.0f });
            menu->Show(Context, screenPos.x, screenPos.y);
        }

        EditorContext* m_editor;    // borrowed (project + resources)
        SceneEditContext* m_edit;   // borrowed (the page owns it)
        RefPtr<tk::PropertyGrid> m_grid;
        RefPtr<ui::Button> m_addButton;
        Array<Function<void()>> m_refreshers;
        u64 m_signature = ~0ull;
    };

    DRACONIC_DEFINE_OBJECT(ResourceRefEditor, "draconic::editor")
    DRACONIC_DEFINE_OBJECT(SceneInspectorView, "draconic::editor")
}
