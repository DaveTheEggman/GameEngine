// Editor::Scene - :inspector partition.
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

export module editor.scene:inspector;

import foundation.core;
import foundation.content;
import foundation.resource;
import foundation.geometry;
import foundation.animation;
import foundation.materials;
import foundation.texture.resource;
import foundation.particles.resource;
import foundation.scene;
import engine.render;
import foundation.physics;
import foundation.physics.resource;
import engine.physics;
import foundation.audio;
import foundation.audio.resource;
import engine.audio;
import foundation.ui.resource;
import foundation.script.resource;
import engine.script;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;
import :edit;

using namespace foundation::core;
namespace core = foundation::core;

export namespace editor
{
    namespace ui = foundation::ui;
    namespace scene = foundation::scene;

    // A property row for resource::Ref fields, backed by the composite AssetPickerSlot
    // (asset-picker-slot.md): [type icon | name (click = picker) | Edit | Clear]. Affordances
    // render only when their callback is wired, so non-asset consumers (the entity-ref twin)
    // degrade to a plain name button. The value text refreshes from the ref's Guid each frame;
    // "(none)" (AssetNameFor's nil spelling) marks the slot empty.
    class ResourceRefEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(ResourceRefEditor, ui::toolkit::PropertyEditor)
    public:
        Function<void()> OnPick;   // opens the picker (wired by the inspector)
        Function<void()> OnEdit;   // open-for-editing (EditorContext::OpenAsset routing)
        Function<void()> OnClear;  // clears the ref through the consumer's undoable command
        Function<void()> OnReveal; // reveal in the asset browser (EditorContext::RevealAsset)
        Function<void(const Guid&)> OnAssignDropped; // browser drag-drop assign (same command)
        Function<void(StringView, StringView)> OnRejectedDrop; // wrong-type drop -> toast

        ResourceRefEditor(StringView name, StringView valueText, StringView category)
            : ui::toolkit::PropertyEditor(name, category), m_valueText(valueText)
        {
        }

        void SetValueText(StringView text);
        /// The asset TYPE glyph for the slot preview (EditorIcons::ForAssetType); set before
        /// the grid builds the row.
        void SetPreviewIcon(ui::SVGDrawable* icon) { m_previewIcon = icon; }
        /// The accepted asset-type names (picker filter + drop filter); set before the grid
        /// builds the row.
        void SetAcceptedTypes(Array<String> types) { m_acceptedTypes = Move(types); }
        /// The generated thumbnail (wins over the type icon while set; empty falls back).
        /// Queried per refresh by the row builders - cheap map lookup.
        void SetPreviewThumbnail(ui::DrawablePtr thumbnail)
        {
            if (m_slot.Get() != nullptr)
            {
                m_slot->SetPreviewThumbnail(Move(thumbnail));
            }
        }

        void RefreshView() override {}

    protected:
        RefPtr<ui::View> CreateEditorView() override;

    private:
        [[nodiscard]] bool HasValue() const
        {
            return m_valueText.AsView() != StringView(u8"(none)");
        }

        String m_valueText;
        ui::SVGDrawable* m_previewIcon = nullptr; // borrowed (EditorIcons)
        Array<String> m_acceptedTypes;
        RefPtr<editor::app::AssetPickerSlot> m_slot;
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
        String prop;       // the dependent property's reflected name
        Array<i64> values; // empty = truthy test
    };

    [[nodiscard]] inline bool ParsePropertyCondition(StringView spec, PropertyCondition& out)
    {
        const utf8char* d = spec.Data();
        usize eq = spec.Size();
        for (usize i = 0; i < spec.Size(); ++i)
        {
            if (d[i] == u8'=')
            {
                eq = i;
                break;
            }
        }
        if (eq == 0)
        {
            return false;
        }
        out.prop = String(spec.SubStr(0, eq));
        out.values.Clear();
        if (eq == spec.Size())
        {
            return true;
        } // truthy form
        i64 value = 0;
        bool negative = false;
        bool any = false;
        for (usize i = eq + 1; i <= spec.Size(); ++i)
        {
            const utf8char c = (i < spec.Size()) ? d[i] : u8','; // sentinel comma flushes
            if (c == u8',')
            {
                if (!any)
                {
                    return false;
                }
                out.values.PushBack(negative ? -value : value);
                value = 0;
                negative = false;
                any = false;
            }
            else if (c == u8'-' && !any && !negative)
            {
                negative = true;
            }
            else if (c >= u8'0' && c <= u8'9')
            {
                value = value * 10 + (c - u8'0');
                any = true;
            }
            else
            {
                return false;
            }
        }
        return !out.values.IsEmpty();
    }

    [[nodiscard]] inline bool MatchesPropertyCondition(const PropertyCondition& condition, i64 raw)
    {
        if (condition.values.IsEmpty())
        {
            return raw != 0;
        }
        for (i64 v : condition.values)
        {
            if (v == raw)
            {
                return true;
            }
        }
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
            if (i == 0 && lower)
            {
                c = static_cast<utf8char>(c - (u8'a' - u8'A'));
            }
            else if (upper)
            {
                const bool nextLower =
                    (i + 1 < name.Size()) && (d[i + 1] >= u8'a' && d[i + 1] <= u8'z');
                if (prevLower || (prevUpper && nextLower))
                {
                    out += u8' ';
                }
            }
            out += c;
            prevLower = lower;
            prevUpper = upper;
        }
        return out;
    }

    // Bespoke editor for the physics collision-group matrix (a shape reflection rows
    // can't express): one row per named group - name field + a toggle per column group.
    // Symmetric by construction (a toggle writes BOTH directions); every edit is one
    // whole-block undoable command, and the inspector's structural rebuild re-reads.
    class CollisionMatrixEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(CollisionMatrixEditor, ui::toolkit::PropertyEditor)
    public:
        Array<String> names; // display names (index = group)
        Array<u32> matrix;   // parallel collide masks
        Function<void(usize, String)> OnRename;
        Function<void(usize, usize)> OnToggle; // (row group, column group)
        Function<void()> OnAddGroup;

        CollisionMatrixEditor(StringView name, StringView category)
            : ui::toolkit::PropertyEditor(name, category)
        {
        }

        void RefreshView() override {}

        /// Rebuild the grid after a STRUCTURAL change (add group / rename / toggle). Deferred via the
        /// UI mutation queue: the trigger is a button-click event, and rebuilding tears down the very
        /// views dispatching it, which must not happen mid-event (UIContext mutation-queue rule).
        void RequestRebuild();

    protected:
        RefPtr<ui::View> CreateEditorView() override;

    private:
        void BuildGrid(ui::FlexLayout& column); // the row/cell/add-button build (rerun on rebuild)
        RefPtr<ui::FlexLayout> m_column;         // the editor view, kept so a rebuild can repopulate it
    };

    // A read-only inspector NOTICE row: a wrapped advisory label spanning the editor column (no value
    // editing). Used for conditional hints like "RigidBody shape=Cooked but no collision shape set".
    class NoticeEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(NoticeEditor, ui::toolkit::PropertyEditor)
    public:
        String message;
        NoticeEditor(StringView name, StringView category)
            : ui::toolkit::PropertyEditor(name, category)
        {
        }
        void RefreshView() override {}

    protected:
        RefPtr<ui::View> CreateEditorView() override;
    };

    // The generic list-of-asset-slots editor (add icon + AssetPickerSlot rows with move/remove) now
    // lives in the shared editor.app layer as editor::app::ContainerListEditor, so bespoke asset pages
    // reuse the identical widget. Brought into this namespace below for the inspector's use sites.
    using editor::app::ContainerListEditor;

    class SceneInspectorView : public ui::ViewGroup
    {
        RTTI_OBJECT(SceneInspectorView, ui::ViewGroup)
    public:
        SceneInspectorView(EditorContext& editor, SceneEditContext& edit)
            : m_editor(&editor), m_edit(&edit)
        {
            // Two tabs: Entity (the selected entity's sections + Add/Paste) and Scene (the scene's
            // settings). The Scene tab makes scene-settings a first-class view reachable anytime,
            // instead of requiring a deselect (empty-viewport click) to surface them.
            m_tabView = MakeRef<ui::TabView>(DefaultAllocator());
            m_tabView->TabsClosable.SetValue(false);
            {
                SceneInspectorView* self = this;
                m_tabView->OnTabChanged.Add(
                    [self](ui::TabView*, i32) { self->m_forceRebuild = true; });
            }

            // --- Entity tab ---
            auto entityColumn = MakeRef<ui::FlexLayout>(DefaultAllocator());
            entityColumn->Direction = ui::Orientation::Vertical;
            entityColumn->Padding = ui::Thickness{8, 6}; // inset off the panel edge (like hierarchy)

            m_emptyLabel = MakeRef<ui::Label>(DefaultAllocator(),
                                              StringView(u8"Select an entity to inspect."));
            m_emptyLabel->FontSize.SetValue(12.0f);
            m_emptyLabel->Visibility = ui::Visibility::Gone; // shown only when nothing is selected
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                entityColumn->AddView(m_emptyLabel.Get(), lp);
            }

            m_entityGrid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                entityColumn->AddView(m_entityGrid.Get(), grow);
            }

            m_addButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Add Component"));
            {
                SceneInspectorView* self = this;
                m_addButton->OnClick.Add([self](ui::ButtonBase*) { self->ShowAddComponentMenu(); });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                entityColumn->AddView(m_addButton.Get(), lp);
            }

            // Paste Component: below Add Component, shown only when the clipboard holds a component
            // (UpdatePasteButton, run each Refresh). Pasting over an existing same-type component
            // overwrites it, so that case asks for confirmation first.
            m_pasteButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Paste Component"));
            {
                SceneInspectorView* self = this;
                m_pasteButton->OnClick.Add([self](ui::ButtonBase*) { self->PasteSelectedComponent(); });
                m_pasteButton->Visibility = ui::Visibility::Gone;
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Margin = ui::Thickness{0.0f, 6.0f, 0.0f, 0.0f}; // gap below Add Component
                entityColumn->AddView(m_pasteButton.Get(), lp);
            }

            // --- Scene tab ---
            auto sceneColumn = MakeRef<ui::FlexLayout>(DefaultAllocator());
            sceneColumn->Direction = ui::Orientation::Vertical;
            sceneColumn->Padding = ui::Thickness{8, 6};
            m_sceneGrid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                sceneColumn->AddView(m_sceneGrid.Get(), grow);
            }

            m_tabView->AddTab(u8"Entity", entityColumn.Get());
            m_tabView->AddTab(u8"Scene", sceneColumn.Get());
            m_grid = m_entityGrid; // the active grid; Rebuild re-points it to the selected tab

            AddView(m_tabView.Get());
        }

        /// Per-frame: structural rebuild when the shape changed, else pull values into widgets.
        void Refresh();

        [[nodiscard]] ui::toolkit::PropertyGrid* Grid() const noexcept { return m_grid.Get(); }

        // Fill the available space (wrap-to-children would collapse the scrolling grid).
        void OnMeasure(ui::BoxConstraints constraints) override;
        void OnLayout(f32, f32, f32 width, f32 height) override;

    private:
        // TypeOf<T> for a type nobody registered still exists, named "<value>" - such
        // components (e.g. from a game module without reflection) can't be edited or even
        // sensibly LISTED, so the Add menu skips them and sections fall back to the manager's
        // serialization id when available.
        [[nodiscard]] static bool IsRegisteredType(const TypeInfo* type);

        [[nodiscard]] Guid SelectedEntity() const;

        // Selected entity + scene revision + which managers have a component on it.
        [[nodiscard]] u64 Signature();

        void Rebuild();

        void BuildEntitySection(const Guid& id);

        void BuildTransformSection(const Guid& id);

        void BuildSceneSettingsSections();

        // The collision-group matrix (physics settings): a bespoke grid row editing the
        // names + symmetric collide matrix through whole-block undoable commands.
        void BuildCollisionMatrixRow(const TypeInfo* type, StringView category);

        // A scene-setting property row: same editor kinds as components, but reading the
        // system's settings instance and writing through SetSceneSettingProperty commands
        // (merged scrubs, one undo entry). Covers the kinds settings blocks use today.
        void BuildSettingRow(const TypeInfo* type, const PropertyInfo& prop, StringView category);

        void BuildComponentSection(const Guid& id, scene::ComponentManagerBase& mgr);

        // Raw integral value of a bool/enum/int property via the address escape hatch.
        [[nodiscard]] static i64 RawIntValue(const Instance& obj, const PropertyInfo& p);

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
                        if (StringView(reinterpret_cast<const utf8char*>(p.name)) ==
                            condition.prop.AsView())
                        {
                            dependent = &p;
                            break;
                        }
                    }
                }
            }

            for (usize i = firstRow; i < m_grid->PropertyCount(); ++i)
            {
                ui::toolkit::PropertyEditor* editor = m_grid->PropertyAt(i);
                const String* label =
                    (displayName != nullptr) ? displayName->value.TryGet<String>() : nullptr;
                editor->SetDisplayName(label != nullptr
                                           ? label->AsView()
                                           : PrettifyPropertyName(editor->Name()).AsView());
                if (description != nullptr)
                {
                    if (const String* s = description->value.TryGet<String>())
                    {
                        editor->SetTooltip(s->AsView());
                    }
                }
                if (dependent != nullptr)
                {
                    auto refresh = [editor, dependent, condition, get = instance]()
                    {
                        const Instance obj = get();
                        editor->SetRowVisible(
                            !obj.IsEmpty() &&
                            MatchesPropertyCondition(condition, RawIntValue(obj, *dependent)));
                    };
                    refresh();
                    m_refreshers.PushBack(Function<void()>{Move(refresh)});
                }
            }
        }

        void BuildPropertyRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                              StringView category);

        // Generic reflected CONTAINER property (a reflected list member): one grid row whose editor is
        // a ContainerListEditor - a header add + a slot row per element (an asset-picker slot + move /
        // remove icons). Slot text + the pick/add/remove/move callbacks are wired here to the reflection
        // container ops via MutateComponent (one undo step each); a content-diff refresher forces a
        // rebuild when the list changes (which Signature() does not track). Element pick is currently
        // specialized to Ref<Material>; struct-element leaf editing + the polymorphic add-by-type menu
        // are a follow-up.
        void BuildContainerRows(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                                StringView category);
        // One undoable mutation of a component via a generic snapshot/restore/paste (any component type
        // - unlike the typed Mutate*Component helpers): copy the live value, mutate live, snapshot,
        // restore (non-undoable ReadComponent), PASTE (the paste command captures the pre-state).
        void MutateComponent(const Guid& id, const TypeInfo* type,
                             const Function<void(const Instance&)>& mutate);

        // Current target Guid of a Ref<T> property (nil when unset/unresolvable).
        template <typename T>
        [[nodiscard]] Guid RefTarget(const Guid& id, const TypeInfo* type, const char* propName)
        {
            scene::ComponentManagerBase* mgr = m_edit->FindManager(type);
            const scene::EntityHandle e = m_edit->Resolve(id);
            if (mgr == nullptr || !e.IsAssigned())
            {
                return Guid{};
            }
            const Instance component = mgr->GetComponentInstance(e);
            const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
            void* address =
                (p != nullptr && p->address != nullptr) ? p->address(component) : nullptr;
            return (address != nullptr) ? static_cast<foundation::resource::Ref<T>*>(address)->id
                                        : Guid{};
        }

        // One undoable mutation of the selected entity's MeshComponent materials: copy the
        // live value, mutate live, snapshot to a clipboard blob, restore, PASTE (the paste
        // command captures the pre-state, so every slot action is one undo step).
        void MutateMeshMaterials(const Guid& id,
                                 const Function<void(engine::render::MeshComponent&)>& mutate);

        /// The display names the slots editor renders - recomputed by its refresher to
        /// detect shape/content changes (the inspector Signature only sees selection +
        /// component PRESENCE, so a data-only slot mutation or its undo changes nothing it
        /// watches).
        [[nodiscard]] Array<String> MaterialSlotNames(const engine::render::MeshComponent& mc);

        void BuildMaterialSlots(const Guid& id, StringView category);

        // One undoable mutation of the selected entity's ScriptComponent (the mesh-
        // materials pattern: mutate live, snapshot to a clipboard blob, restore, PASTE
        // - the paste command captures the pre-state, so every action is one undo step).
        void
        MutateScriptComponent(const Guid& id,
                              const Function<void(engine::script::ScriptComponent&)>& mutate);

        // The cooked ScriptClass a behavior references (bound through the editor's
        // resource manager so the harvested metadata is available; null when unset or
        // not yet cooked).
        [[nodiscard]] foundation::script::ScriptClass*
        BehaviorClass(const engine::script::ScriptBehavior& behavior);

        // Signature of the behavior list's SHAPE (count + script ids + enabled flags +
        // override counts) - the refresher forces a rebuild when it changes, since the
        // inspector's own Signature only watches selection + component presence.
        [[nodiscard]] u64 ScriptBehaviorsSignature(const engine::script::ScriptComponent& c);

        void BuildScriptBehaviors(const Guid& id, StringView category);

        void BuildScriptBehaviorRows(const Guid& id, StringView category, usize index);

        void BuildScriptPropertyRow(const Guid& id, StringView category, usize index,
                                    const foundation::script::ScriptPropertyDesc& property);

        // Entity-typed property: a picker over the CURRENT scene's entities (a menu of
        // names; the override stores the target's guid).
        void BuildScriptEntityPropertyRow(const Guid& id, StringView category, usize index,
                                          const foundation::script::ScriptPropertyDesc& property);

        // Asset-typed property (asset:<TypeName>): an AssetPickerDialog over that
        // asset type; the override stores the picked guid.
        void BuildScriptAssetPropertyRow(const Guid& id, StringView category, usize index,
                                         const foundation::script::ScriptPropertyDesc& property);

        [[nodiscard]] StringView AssetNameFor(const Guid& target);

        // The settings twin of RefTarget (the Ref lives on a scene system's settings block).
        template <typename T>
        [[nodiscard]] Guid SettingRefTarget(const TypeInfo* type, const char* propName)
        {
            scene::SceneSystem* system = m_edit->FindSystemBySettingsType(type);
            if (system == nullptr)
            {
                return Guid{};
            }
            const Instance settings{system->SettingsInstance(), type};
            const PropertyInfo* p = FindProperty(*type, propName);
            void* address =
                (p != nullptr && p->address != nullptr) ? p->address(settings) : nullptr;
            return (address != nullptr) ? static_cast<foundation::resource::Ref<T>*>(address)->id
                                        : Guid{};
        }

        // The settings twin of BuildResourceRefRow.
        template <typename T>
        void BuildSettingResourceRefRow(const TypeInfo* type, const PropertyInfo& prop,
                                        StringView category,
                                        std::initializer_list<StringView> assetTypeNames)
        {
            SceneInspectorView* self = this;
            SceneEditContext* edit = m_edit;
            const char* propName = prop.name;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));

            auto editor = MakeRef<ResourceRefEditor>(
                DefaultAllocator(), name, AssetNameFor(SettingRefTarget<T>(type, propName)),
                category);
            ResourceRefEditor* raw = editor.Get();
            Array<String> assetTypes;
            for (StringView typeName : assetTypeNames)
            {
                assetTypes.PushBack(String(typeName));
            }
            raw->OnPick = [self, edit, type, propName, assetTypes]()
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                foundation::resource::ResourceManager* resources = self->m_editor->Resources();
                Array<String> typeNames = assetTypes;
                auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_editor, Move(typeNames));
                dialog->OnPicked = [edit, type, propName, resources](const Guid& target)
                { edit->SetSceneSettingResourceRef<T>(type, propName, target, resources); };
                dialog->Show(self->Context);
            };
            if (assetTypes.Size() > 0)
            {
                raw->SetPreviewIcon(
                    editor::app::EditorIcons::Get().ForAssetType(assetTypes[0].AsView()));
            }
            raw->OnClear = [self, edit, type, propName]()
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                edit->SetSceneSettingResourceRef<T>(type, propName, Guid{},
                                                    self->m_editor->Resources());
            };
            raw->OnEdit = [self, type, propName]()
            {
                const Guid target = self->SettingRefTarget<T>(type, propName);
                if (!target.IsNil() && self->m_editor->OpenAsset)
                {
                    self->m_editor->OpenAsset(target);
                }
            };
            raw->OnReveal = [self, type, propName]()
            {
                const Guid target = self->SettingRefTarget<T>(type, propName);
                if (!target.IsNil() && self->m_editor->RevealAsset)
                {
                    self->m_editor->RevealAsset(target);
                }
            };
            raw->SetAcceptedTypes(assetTypes);
            raw->OnAssignDropped = [self, edit, type, propName](const Guid& target)
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                edit->SetSceneSettingResourceRef<T>(type, propName, target,
                                                    self->m_editor->Resources());
            };
            raw->OnRejectedDrop = [self, assetTypes](StringView assetName, StringView typeName)
            {
                self->m_editor->Notify(
                    editor::NoticeKind::Warning,
                    Format(u8"{} is a {} - this field takes {}", assetName, typeName,
                           assetTypes.Size() > 0 ? assetTypes[0].AsView() : StringView(u8"?"))
                        .AsView());
            };
            AddEditor(raw,
                      [self, type, propName, raw]()
                      {
                          const Guid target = self->SettingRefTarget<T>(type, propName);
                          raw->SetValueText(self->AssetNameFor(target));
                          raw->SetPreviewThumbnail(
                              (!target.IsNil() && self->m_editor->Thumbnails() != nullptr)
                                  ? self->m_editor->Thumbnails()->Get(target)
                                  : RefPtr<ui::Drawable>{});
                      });
        }

        template <typename T>
        void BuildResourceRefRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                                 StringView category,
                                 std::initializer_list<StringView> assetTypeNames)
        {
            SceneInspectorView* self = this;
            SceneEditContext* edit = m_edit;
            const char* propName = prop.name;
            const StringView name(reinterpret_cast<const utf8char*>(prop.name));

            auto editor = MakeRef<ResourceRefEditor>(
                DefaultAllocator(), name, AssetNameFor(RefTarget<T>(id, type, propName)), category);
            ResourceRefEditor* raw = editor.Get();
            Array<String> assetTypes;
            for (StringView typeName : assetTypeNames)
            {
                assetTypes.PushBack(String(typeName));
            }
            raw->OnPick = [self, edit, id, type, propName, assetTypes]()
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                foundation::resource::ResourceManager* resources = self->m_editor->Resources();

                // The browser-mirroring picker (readonly; favorites pinned first; [Clear] = none).
                Array<String> typeNames = assetTypes;
                auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                    DefaultAllocator(), *self->m_editor, Move(typeNames));
                dialog->OnPicked = [edit, id, type, propName, resources](const Guid& target)
                { edit->SetComponentResourceRef<T>(id, type, propName, target, resources); };
                dialog->Show(self->Context);
            };
            if (assetTypes.Size() > 0)
            {
                raw->SetPreviewIcon(
                    editor::app::EditorIcons::Get().ForAssetType(assetTypes[0].AsView()));
            }
            raw->OnClear = [self, edit, id, type, propName]()
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                edit->SetComponentResourceRef<T>(id, type, propName, Guid{},
                                                 self->m_editor->Resources());
            };
            raw->OnEdit = [self, id, type, propName]()
            {
                const Guid target = self->RefTarget<T>(id, type, propName);
                if (!target.IsNil() && self->m_editor->OpenAsset)
                {
                    self->m_editor->OpenAsset(target);
                }
            };
            raw->OnReveal = [self, id, type, propName]()
            {
                const Guid target = self->RefTarget<T>(id, type, propName);
                if (!target.IsNil() && self->m_editor->RevealAsset)
                {
                    self->m_editor->RevealAsset(target);
                }
            };
            raw->SetAcceptedTypes(assetTypes);
            raw->OnAssignDropped = [self, edit, id, type, propName](const Guid& target)
            {
                if (self->Context == nullptr || self->m_editor->Project() == nullptr)
                {
                    return;
                }
                edit->SetComponentResourceRef<T>(id, type, propName, target,
                                                 self->m_editor->Resources());
            };
            raw->OnRejectedDrop = [self, assetTypes](StringView assetName, StringView typeName)
            {
                self->m_editor->Notify(
                    editor::NoticeKind::Warning,
                    Format(u8"{} is a {} - this field takes {}", assetName, typeName,
                           assetTypes.Size() > 0 ? assetTypes[0].AsView() : StringView(u8"?"))
                        .AsView());
            };
            AddEditor(raw,
                      [self, id, type, propName, raw]()
                      {
                          const Guid target = self->RefTarget<T>(id, type, propName);
                          raw->SetValueText(self->AssetNameFor(target));
                          raw->SetPreviewThumbnail(
                              (!target.IsNil() && self->m_editor->Thumbnails() != nullptr)
                                  ? self->m_editor->Thumbnails()->Get(target)
                                  : RefPtr<ui::Drawable>{});
                      });
        }

        // Entity-reference row: the entity-picker twin of BuildResourceRefRow. Same ResourceRefEditor
        // widget, but the pick menu lists the CURRENT scene's entities (names; "(none)" clears) and
        // the choice writes the component's EntityRef via SetComponentEntityRef. Defined in the impl.
        void BuildEntityRefRow(const Guid& id, const TypeInfo* type, const PropertyInfo& prop,
                               StringView category);

        // The "range" attribute's {min, max, step} payload, or null when absent/mistyped.
        [[nodiscard]] static const Float4* RangeOf(const PropertyInfo& prop);

        void AddEditor(ui::toolkit::PropertyEditor* editor, Function<void()> refresher);

        [[nodiscard]] static Float3 EulerDegrees(Quaternion q);

        void ShowAddComponentMenu();
        // Paste the clipboard component onto the selected entity; if it already has that component
        // type the paste OVERWRITES it, so confirm first (it is undoable either way).
        void PasteSelectedComponent();
        // Show/hide the Paste button based on whether the clipboard holds a component (per Refresh).
        void UpdatePasteButton();

        static constexpr i32 kEntityTab = 0;
        static constexpr i32 kSceneTab = 1;

        EditorContext* m_editor;  // borrowed (project + resources)
        SceneEditContext* m_edit; // borrowed (the page owns it)
        RefPtr<ui::TabView> m_tabView;
        RefPtr<ui::toolkit::PropertyGrid> m_grid; // the ACTIVE tab's grid (re-pointed each Rebuild)
        RefPtr<ui::toolkit::PropertyGrid> m_entityGrid;
        RefPtr<ui::toolkit::PropertyGrid> m_sceneGrid;
        RefPtr<ui::Label> m_emptyLabel; // "Select an entity to inspect." (Entity tab, no selection)
        RefPtr<ui::Button> m_addButton;
        RefPtr<ui::Button> m_pasteButton;
        Array<Function<void()>> m_refreshers;
        Guid m_lastSelectedForTab; // selection last seen (auto-switch to Entity tab on a new pick)
        u64 m_signature = ~0ull;
        bool m_forceRebuild = false; // set when a data-only mutation changed a section's SHAPE
    };

    RTTI_DEFINE_OBJECT(ResourceRefEditor, "rtti::editor::editor")
    RTTI_DEFINE_OBJECT(NoticeEditor, "rtti::editor::editor")
    RTTI_DEFINE_OBJECT(CollisionMatrixEditor, "rtti::editor::editor")
    // ContainerListEditor's RTTI define moved with the class to editor.app (ContainerListEditorImpl.cpp).
    RTTI_DEFINE_OBJECT(SceneInspectorView, "rtti::editor::editor")
}
