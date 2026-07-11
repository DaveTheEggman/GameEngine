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

export module draconic.editor.scene:inspector;

import draconic.core;
import draconic.scene;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import :edit;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::editor
{
    namespace ui = draconic::ui;
    namespace tk = draconic::ui::toolkit;
    namespace dscene = draconic::scene;

    class SceneInspectorView : public ui::ViewGroup
    {
        DRACONIC_OBJECT(SceneInspectorView, ui::ViewGroup)
    public:
        explicit SceneInspectorView(SceneEditContext& edit) : m_edit(&edit)
        {
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;

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
            if (!e.IsAssigned()) { Invalidate(); return; }

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

        void BuildComponentSection(const Guid& id, dscene::ComponentManagerBase& mgr)
        {
            const TypeInfo* type = mgr.ComponentType();
            if (type == nullptr) { return; }
            const StringView fallback = mgr.SerializationTypeId();
            const StringView category = IsRegisteredType(type)
                ? StringView(reinterpret_cast<const utf8char*>(type->name))
                : (fallback.IsEmpty() ? StringView(u8"(unreflected component)") : fallback);

            for (const PropertyInfo& prop : Properties(*type))
            {
                BuildPropertyRow(id, type, prop, category);
            }

            SceneEditContext* edit = m_edit;
            auto remove = MakeRef<tk::ButtonEditor>(DefaultAllocator(), StringView(u8"Remove"),
                Function<void()>{ [edit, id, type]() { edit->RemoveComponent(id, type); } }, category);
            m_grid->AddProperty(RefPtr<tk::PropertyEditor>(remove.Get()));
        }

        void BuildPropertyRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                              StringView category)
        {
            SceneEditContext* edit = m_edit;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));
            const bool readOnly = (static_cast<u32>(prop.flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0;
            const char* propName = prop.name;

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
            const Float2 screenPos = m_addButton->LocalToScreen(Float2{ 0.0f, 0.0f });
            menu->Show(Context, screenPos.x, screenPos.y);
        }

        SceneEditContext* m_edit;   // borrowed (the page owns it)
        RefPtr<tk::PropertyGrid> m_grid;
        RefPtr<ui::Button> m_addButton;
        Array<Function<void()>> m_refreshers;
        u64 m_signature = ~0ull;
    };

    DRACONIC_DEFINE_OBJECT(SceneInspectorView, "draconic::editor")
}
