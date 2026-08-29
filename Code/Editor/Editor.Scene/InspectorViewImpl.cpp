// Editor::Scene - :inspector partition.
//
// SceneInspectorView: the reflection-driven property inspector INSIDE a scene page (per-page,
// like everything scene-scoped). A toolkit PropertyGrid rebuilt from the primary
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

module editor.scene;

import foundation.core;
import foundation.content;
import foundation.resource;
import foundation.geometry;
import foundation.animation;
import foundation.materials;
import foundation.texture.resource;
import foundation.particles.resource;
import foundation.propertyanimation.resource;
import foundation.scene;
import engine.render;
import foundation.physics;
import foundation.physics.resource;
import foundation.heightfield; // Ref<Heightfield> picker (heightfield collider)
import foundation.terrain.resource; // Ref<TerrainResource> picker (TerrainComponent)
import engine.physics;
import foundation.navigation.resource;
import engine.navigation;
import editor.navigation;
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
namespace scene = foundation::scene;
namespace ui = foundation::ui;
namespace core = foundation::core;
using editor::app::ContainerListEditor; // moved to editor.app (shared list widget)

namespace editor
{
    RefPtr<ui::View> NoticeEditor::CreateEditorView()
    {
        auto label = MakeRef<ui::Label>(DefaultAllocator(), message.AsView());
        label->WordWrap.SetValue(true);
        label->TextColor.SetValue(
            Optional<core::Color>{core::Color{0.95f, 0.75f, 0.2f, 1.0f}}); // amber advisory
        return label;
    }

    RefPtr<ui::View> CollisionMatrixEditor::CreateEditorView()
    {
        m_column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_column->Direction = ui::Orientation::Vertical;
        m_column->Spacing = 2.0f;
        BuildGrid(*m_column);
        return m_column;
    }

    void CollisionMatrixEditor::RequestRebuild()
    {
        if (m_column.Get() == nullptr)
        {
            return;
        }
        RefPtr<CollisionMatrixEditor> self(this); // keep alive across the deferred drain
        auto rebuild = [self]()
        {
            self->m_column->RemoveAllViews(/*deleteChildren=*/true);
            self->BuildGrid(*self->m_column);
            self->m_column->Invalidate();
        };
        // Structural mutation: defer to the mutation queue so the click-dispatching views are not torn
        // down mid-event. Not yet attached (no context) -> safe to rebuild inline.
        if (ui::UIContext* ctx = m_column->Context)
        {
            ctx->MutationQueueRef().QueueAction(Function<void()>{Move(rebuild)});
        }
        else
        {
            rebuild();
        }
    }

    void CollisionMatrixEditor::BuildGrid(ui::FlexLayout& column)
    {
        CollisionMatrixEditor* self = this;
        const usize count = names.Size();
        // Snapshot the committed names (see the member comment: blur-commit change detection).
        committedNames.Clear();
        for (const String& n : names)
        {
            committedNames.PushBack(String(n.AsView()));
        }

        // A proper labeled matrix: group names down the LEFT (editable), VERTICAL names across the TOP
        // (rotated -90 so narrow columns stay narrow - the horizontal version clipped in the inspector),
        // and a checkbox at every crossing. Fixed column widths keep the header + every row aligned.
        constexpr f32 kNameColW = 104.0f;         // the left name column
        constexpr f32 kCellW = 26.0f;             // each group column (narrow; vertical headers)
        constexpr f32 kRowH = 22.0f;
        constexpr f32 kHeaderH = 88.0f;           // room for the rotated names
        constexpr f32 kQuarterTurn = -1.5707963f; // -90 deg: header names read bottom-to-top

        auto fixedCell = [](f32 width)
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(width));
            return lp;
        };
        auto centeredSlot = []()
        {
            auto slot = MakeRef<ui::FlexLayout>(DefaultAllocator());
            slot->Direction = ui::Orientation::Horizontal;
            slot->JustifyContent = ui::Justify::Center;
            slot->AlignItems = ui::Align::Center;
            return slot;
        };

        // Header row: an empty corner over the name column, then a VERTICAL name per group column.
        {
            auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
            header->Direction = ui::Orientation::Horizontal;
            header->Spacing = 2.0f;
            header->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"")).Get(),
                            fixedCell(kNameColW));
            for (usize j = 0; j < count; ++j)
            {
                auto slot = centeredSlot();
                auto head = MakeRef<ui::Label>(DefaultAllocator(), names[j].AsView());
                head->FontSize.SetValue(Optional<f32>{11.0f});
                head->TooltipText = names[j];
                head->Transform.Rotation = kQuarterTurn; // vertical
                head->Transform.Origin = Float2{0.5f, 0.5f};
                slot->AddView(head.Get(), MakeRef<ui::FlexLayoutParams>(DefaultAllocator()));
                header->AddView(slot.Get(), fixedCell(kCellW));
            }
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(kHeaderH));
            column.AddView(header.Get(), lp);
        }

        for (usize i = 0; i < count; ++i)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 2.0f;

            // Row header: the editable group name on the LEFT.
            auto name = MakeRef<ui::EditText>(DefaultAllocator());
            name->SetText(names[i].AsView());
            ui::EditText* nameRaw = name.Get();
            name->OnSubmit.Add(
                [self, i, nameRaw](ui::EditText*)
                {
                    if (self->OnRename)
                    {
                        self->OnRename(i, String(nameRaw->Text()));
                    }
                });
            // Mirror the typed text into the in-memory name list every keystroke (NO commit/rebuild -
            // that would steal focus mid-edit). EditText only fires OnSubmit on Enter/activate, NOT on
            // blur, so clicking Add/Remove/a checkbox after typing would otherwise drop an un-submitted
            // rename; the structural handlers read `names`, so this keeps their edited copy current.
            name->OnTextChanged.Add(
                [self, i, nameRaw](ui::EditText*)
                {
                    if (i < self->names.Size())
                    {
                        self->names[i] = String(nameRaw->Text());
                    }
                });
            // Commit on blur too: a rename typed and then clicked-away-from (another entity, save,
            // panel close) must not live only in the display copy. Change-detected against the
            // committed snapshot so an untouched field pushes no undo entry, and an Enter-committed
            // rename (rebuild refreshes the snapshot) is not committed twice.
            name->OnEditingFinished.Add(
                [self, i, nameRaw](ui::EditText*)
                {
                    if (i >= self->committedNames.Size() || !self->OnRename)
                    {
                        return;
                    }
                    const String typed(nameRaw->Text());
                    if (typed.AsView() != self->committedNames[i].AsView())
                    {
                        self->OnRename(i, String(typed.AsView()));
                    }
                });
            row->AddView(name.Get(), fixedCell(kNameColW));

            // A centered checkbox at each crossing; symmetric (OnToggle flips both (i,j) and (j,i)).
            for (usize j = 0; j < count; ++j)
            {
                const bool collides = i < matrix.Size() && (matrix[i] & (1u << j)) != 0;
                auto slot = centeredSlot();
                auto box = MakeRef<ui::CheckBox>(DefaultAllocator(), StringView(u8""), collides);
                box->OnCheckedChanged.Add(
                    [self, i, j](ui::CheckBox*, bool)
                    {
                        if (self->OnToggle)
                        {
                            self->OnToggle(i, j);
                        }
                    });
                slot->AddView(box.Get(), MakeRef<ui::FlexLayoutParams>(DefaultAllocator()));
                row->AddView(slot.Get(), fixedCell(kCellW));
            }

            // Remove: offered on the LAST group only - popping the tail never renumbers the groups
            // BELOW it, so entities keep their assigned collisionGroup (a mid-list delete would shift
            // every higher index and silently re-group bodies). Keep at least one group.
            if (i + 1 == count && count > 1)
            {
                auto del = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"x"));
                del->FontSize.SetValue(Optional<f32>{12.0f});
                del->TooltipText = String(u8"Remove this group (the last one)");
                del->OnClick.Add(
                    [self, i](ui::ButtonBase*)
                    {
                        if (self->OnRemoveGroup)
                        {
                            self->OnRemoveGroup(i);
                        }
                    });
                row->AddView(del.Get(), fixedCell(20.0f));
            }

            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(kRowH));
            column.AddView(row.Get(), lp);
        }

        if (count < foundation::physics::kCollisionGroupCount)
        {
            auto add = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"+ Add Group"));
            add->FontSize.SetValue(Optional<f32>{12.0f});
            add->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    if (self->OnAddGroup)
                    {
                        self->OnAddGroup();
                    }
                });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(kRowH));
            column.AddView(add.Get(), lp);
        }
    }

    void ResourceRefEditor::SetValueText(StringView text)
    {
        if (m_valueText.AsView() == text)
        {
            return;
        }
        m_valueText = String(text);
        if (m_slot.Get() != nullptr)
        {
            m_slot->SetValue(m_valueText.AsView(), HasValue());
        }
    }

    RefPtr<ui::View> ResourceRefEditor::CreateEditorView()
    {
        m_slot = MakeRef<editor::app::AssetPickerSlot>(DefaultAllocator());
        ResourceRefEditor* self = this;
        // Forward only the affordances the consumer wired - unwired ones stay hidden.
        if (OnPick)
        {
            m_slot->OnPick = [self]() { self->OnPick(); };
        }
        if (OnEdit)
        {
            m_slot->OnEdit = [self]() { self->OnEdit(); };
        }
        if (OnClear)
        {
            m_slot->OnClear = [self]() { self->OnClear(); };
        }
        if (OnReveal)
        {
            m_slot->OnReveal = [self]() { self->OnReveal(); };
        }
        if (OnAssignDropped)
        {
            m_slot->OnAssignDropped = [self](const Guid& id) { self->OnAssignDropped(id); };
        }
        if (OnRejectedDrop)
        {
            m_slot->OnRejectedDrop = [self](StringView assetName, StringView typeName)
            { self->OnRejectedDrop(assetName, typeName); };
        }
        m_slot->SetAcceptedTypes(m_acceptedTypes);
        m_slot->SetPreviewIcon(m_previewIcon);
        m_slot->SetValue(m_valueText.AsView(), HasValue());
        return RefPtr<ui::View>(m_slot.Get());
    }
    void SceneInspectorView::Refresh()
    {
        // Auto-switch to the Entity tab when a NEW entity is selected (selecting implies you want to
        // inspect it). Deselection stays on the current tab - the Entity tab then shows its empty
        // hint, and the Scene tab stays reachable without any viewport click.
        const Guid selected = SelectedEntity();
        if (selected != m_lastSelectedForTab)
        {
            m_lastSelectedForTab = selected;
            if (m_edit->Resolve(selected).IsAssigned() && m_tabView.Get() != nullptr &&
                m_tabView->SelectedIndex() != kEntityTab)
            {
                m_tabView->SetSelectedIndex(kEntityTab); // fires OnTabChanged -> forceRebuild
            }
        }

        UpdatePasteButton(); // clipboard can change any frame; keep the Paste button in sync
        const u64 signature = Signature();
        if (m_forceRebuild || signature != m_signature)
        {
            m_forceRebuild = false;
            m_signature = signature;
            Rebuild();
        }
        else
        {
            for (const Function<void()>& refresher : m_refreshers)
            {
                refresher();
            }
        }
    }

    void SceneInspectorView::OnMeasure(ui::BoxConstraints constraints)
    {
        for (usize i = 0; i < ChildCount(); ++i)
        {
            GetChildAt(i)->Measure(constraints);
        }
        MeasuredSize = Float2{constraints.MaxWidth, constraints.MaxHeight};
    }

    void SceneInspectorView::OnLayout(f32, f32, f32 width, f32 height)
    {
        for (usize i = 0; i < ChildCount(); ++i)
        {
            GetChildAt(i)->Layout(0, 0, width, height);
        }
    }

    bool SceneInspectorView::IsRegisteredType(const TypeInfo* type)
    {
        if (type == nullptr || type->name == nullptr)
        {
            return false;
        }
        const char* n = type->name;
        return !(n[0] == '<');
    }

    Guid SceneInspectorView::SelectedEntity() const
    {
        const Guid* primary = m_edit->EntitySelection().Primary();
        return (primary != nullptr) ? *primary : Guid{};
    }

    u64 SceneInspectorView::Signature()
    {
        const Guid id = SelectedEntity();
        u64 signature = id.high ^ (id.low * 0x9E3779B97F4A7C15ull) ^ m_edit->Scene().Revision();
        const scene::EntityHandle e = m_edit->Resolve(id);
        if (e.IsAssigned())
        {
            u64 bit = 1;
            m_edit->Scene().ForEachManager(
                [&](scene::ComponentManagerBase& mgr)
                {
                    if (mgr.HasComponent(e))
                    {
                        signature ^= bit * 0xBF58476D1CE4E5B9ull;
                    }
                    bit <<= 1;
                });
        }
        return signature;
    }

    void SceneInspectorView::Rebuild()
    {
        // The active tab drives which grid we build into (the Scene tab shows scene settings
        // regardless of selection; the Entity tab shows the selected entity, or an empty hint).
        const bool sceneTab = m_tabView.Get() != nullptr && m_tabView->SelectedIndex() == kSceneTab;
        m_grid = sceneTab ? m_sceneGrid : m_entityGrid;
        m_grid->Clear();
        m_refreshers.Clear();

        // Scene tab: the SCENE's settings (Sedulous scene-modules pattern) - every scene system
        // exposing a reflected settings block gets a category.
        if (sceneTab)
        {
            BuildSceneSettingsSections();
            Invalidate();
            return;
        }

        // Entity tab.
        const Guid id = SelectedEntity();
        const scene::EntityHandle e = m_edit->Resolve(id);
        const bool has = e.IsAssigned();
        m_addButton->Visibility = has ? ui::VisibilityValue::Visible : ui::VisibilityValue::Gone;
        m_emptyLabel->Visibility = has ? ui::VisibilityValue::Gone : ui::VisibilityValue::Visible;
        if (!has)
        {
            Invalidate();
            return;
        }

        BuildEntitySection(id);
        BuildTransformSection(id);

        m_edit->Scene().ForEachManager(
            [&](scene::ComponentManagerBase& mgr)
            {
                const scene::EntityHandle live = m_edit->Resolve(id);
                if (live.IsAssigned() && mgr.HasComponent(live))
                {
                    BuildComponentSection(id, mgr);
                }
            });
        Invalidate();
    }

    void SceneInspectorView::BuildEntitySection(const Guid& id)
    {
        SceneEditContext* edit = m_edit;

        auto name = MakeRef<ui::toolkit::StringEditor>(
            DefaultAllocator(), StringView(u8"Name"),
            m_edit->Scene().GetEntityName(m_edit->Resolve(id)),
            Function<void(StringView)>{[edit, id](StringView v) { edit->RenameEntity(id, v); }},
            StringView(u8"Entity"));
        AddEditor(name.Get(), [edit, id, raw = name.Get()]()
                  { raw->SetValue(edit->Scene().GetEntityName(edit->Resolve(id))); });

        auto active = MakeRef<ui::toolkit::BoolEditor>(
            DefaultAllocator(), StringView(u8"Active"),
            m_edit->Scene().IsActive(m_edit->Resolve(id)),
            Function<void(bool)>{[edit, id](bool v) { edit->SetEntityActive(id, v); }},
            StringView(u8"Entity"));
        AddEditor(active.Get(), [edit, id, raw = active.Get()]()
                  { raw->SetValue(edit->Scene().IsActive(edit->Resolve(id))); });
    }

    void SceneInspectorView::BuildTransformSection(const Guid& id)
    {
        SceneEditContext* edit = m_edit;
        const StringView category = u8"Transform";
        const core::Transform t = m_edit->Scene().GetLocalTransform(m_edit->Resolve(id));

        auto position = MakeRef<ui::toolkit::Float3Editor>(
            DefaultAllocator(), StringView(u8"Position"), t.position, -100000.0f, 100000.0f, 0.1f,
            Function<void(Float3)>{[edit, id](Float3 v)
                                   {
                                       core::Transform current =
                                           edit->Scene().GetLocalTransform(edit->Resolve(id));
                                       current.position = v;
                                       edit->SetLocalTransform(id, current);
                                   }},
            category);
        AddEditor(position.Get(), [edit, id, raw = position.Get()]()
                  { raw->SetValue(edit->Scene().GetLocalTransform(edit->Resolve(id)).position); });

        // Rotation displayed as euler DEGREES (x = pitch, y = yaw, z = roll).
        auto rotation = MakeRef<ui::toolkit::Float3Editor>(
            DefaultAllocator(), StringView(u8"Rotation"), EulerDegrees(t.rotation), -360.0f, 360.0f,
            1.0f,
            Function<void(Float3)>{[edit, id](Float3 v)
                                   {
                                       core::Transform current =
                                           edit->Scene().GetLocalTransform(edit->Resolve(id));
                                       current.rotation = FromYawPitchRoll(DegreesToRadians(v.y),
                                                                           DegreesToRadians(v.x),
                                                                           DegreesToRadians(v.z));
                                       edit->SetLocalTransform(id, current);
                                   }},
            category);
        AddEditor(rotation.Get(),
                  [edit, id, raw = rotation.Get()]()
                  {
                      raw->SetValue(EulerDegrees(
                          edit->Scene().GetLocalTransform(edit->Resolve(id)).rotation));
                  });

        auto scale = MakeRef<ui::toolkit::Float3Editor>(
            DefaultAllocator(), StringView(u8"Scale"), t.scale, -100000.0f, 100000.0f, 0.1f,
            Function<void(Float3)>{[edit, id](Float3 v)
                                   {
                                       core::Transform current =
                                           edit->Scene().GetLocalTransform(edit->Resolve(id));
                                       current.scale = v;
                                       edit->SetLocalTransform(id, current);
                                   }},
            category);
        AddEditor(scale.Get(), [edit, id, raw = scale.Get()]()
                  { raw->SetValue(edit->Scene().GetLocalTransform(edit->Resolve(id)).scale); });
    }

    void SceneInspectorView::BuildSceneSettingsSections()
    {
        m_edit->Scene().ForEachSystem(
            [&](scene::SceneSystem& system)
            {
                const TypeInfo* type = system.SettingsType();
                if (type == nullptr || !IsRegisteredType(type))
                {
                    return;
                }
                // Category = the settings type minus a trailing "Settings"
                // ("EnvironmentSettings" -> "Environment").
                StringView category(reinterpret_cast<const utf8char*>(type->name));
                const StringView suffix = u8"Settings";
                if (category.Size() > suffix.Size() &&
                    category.SubStr(category.Size() - suffix.Size(), suffix.Size()) == suffix)
                {
                    category = category.SubStr(0, category.Size() - suffix.Size());
                }
                for (const PropertyInfo& prop : Properties(*type))
                {
                    if (IsNested(prop))
                    {
                        continue; // nested structures are recursed elsewhere, not a leaf row
                    }
                    const usize firstRow = m_grid->PropertyCount();
                    BuildSettingRow(type, prop, category);
                    ApplyPropertyPresentation(
                        type, prop, firstRow,
                        [edit = m_edit, type]() -> Instance
                        {
                            scene::SceneSystem* system = edit->FindSystemBySettingsType(type);
                            return (system != nullptr) ? Instance{system->SettingsInstance(), type}
                                                       : Instance{};
                        });
                }
                if (type == &TypeOf<engine::physics::PhysicsSceneSettings>())
                {
                    BuildCollisionMatrixRow(type, category);
                }
                if (type == &TypeOf<engine::script::SceneScriptSettings>())
                {
                    BuildSceneScriptPropertyRows(type, category); // the Level's [metadata] rows
                }
            });
    }

    void SceneInspectorView::BuildCollisionMatrixRow(const TypeInfo* type, StringView category)
    {
        using engine::physics::PhysicsSceneSettings;
        SceneEditContext* edit = m_edit;
        scene::SceneSystem* system = edit->FindSystemBySettingsType(type);
        if (system == nullptr)
        {
            return;
        }
        auto* live = static_cast<PhysicsSceneSettings*>(system->SettingsInstance());

        auto matrix = MakeRef<CollisionMatrixEditor>(DefaultAllocator(),
                                                     StringView(u8"Collision Groups"), category);
        // Display copy: at least one row ("Default"); rows without a stored mask
        // read as collide-with-everything.
        matrix->names = live->groupNames;
        if (matrix->names.IsEmpty())
        {
            matrix->names.PushBack(String(u8"Default"));
        }
        matrix->matrix = live->groupCollides;
        while (matrix->matrix.Size() < matrix->names.Size())
        {
            matrix->matrix.PushBack(0xFFFFFFFFu);
        }

        auto commit = [edit, type](PhysicsSceneSettings copy)
        {
            MemoryStream buffer;
            BinarySerializer writer(buffer, SerializeMode::Write);
            // Version-wrapped to match SetSceneSettingsBlockCommand's read (Version()-gated fields).
            foundation::core::BeginVersionedPayload(writer, *type);
            engine::physics::SerializePhysicsSceneSettings(writer, copy);
            foundation::core::EndVersionedPayload(writer);
            Array<byte> blob;
            const Span<const byte> bytes = buffer.Bytes();
            blob.Reserve(bytes.Size());
            for (byte b : bytes)
            {
                blob.PushBack(b);
            }
            (void)edit->ApplySceneSettingsBlock(type, Move(blob));
        };
        auto editedCopy = [live, raw = matrix.Get()]()
        {
            PhysicsSceneSettings copy = *live;
            copy.groupNames = raw->names;
            copy.groupCollides = raw->matrix;
            return copy;
        };

        matrix->OnRename = [commit, editedCopy, raw = matrix.Get()](usize i, String name)
        {
            if (i >= raw->names.Size())
            {
                return;
            }
            raw->names[i] = Move(name);
            commit(editedCopy());
            raw->RequestRebuild(); // refresh cell tooltips ("vs <name>")
        };
        matrix->OnToggle = [commit, editedCopy, raw = matrix.Get()](usize i, usize j)
        {
            if (i >= raw->matrix.Size() || j >= raw->matrix.Size())
            {
                return;
            }
            const bool collides = (raw->matrix[i] & (1u << j)) != 0;
            if (collides)
            {
                raw->matrix[i] &= ~(1u << j);
                raw->matrix[j] &= ~(1u << i); // symmetric
            }
            else
            {
                raw->matrix[i] |= (1u << j);
                raw->matrix[j] |= (1u << i);
            }
            commit(editedCopy());
            raw->RequestRebuild(); // flip the +/- cell labels
        };
        matrix->OnAddGroup = [commit, editedCopy, raw = matrix.Get()]()
        {
            String name(u8"Group ");
            const usize index = raw->names.Size();
            if (index >= 10)
            {
                name.PushBack(static_cast<utf8char>('0' + index / 10 % 10));
            }
            name.PushBack(static_cast<utf8char>('0' + index % 10));
            raw->names.PushBack(Move(name));
            raw->matrix.PushBack(0xFFFFFFFFu);
            commit(editedCopy());
            raw->RequestRebuild(); // the new row/column appears (deferred, mutation-queue-safe)
        };
        matrix->OnRemoveGroup = [commit, editedCopy, raw = matrix.Get()](usize index)
        {
            // Only the LAST group is removable: popping the tail leaves every lower index unchanged,
            // so entities keep their assigned collisionGroup. (A mid-list delete would renumber the
            // higher groups and silently re-group bodies - that needs an entity remap, deferred.)
            if (index + 1 != raw->names.Size() || raw->names.Size() <= 1)
            {
                return;
            }
            raw->names.RemoveAt(index);
            raw->matrix.RemoveAt(index);
            const u32 clear = ~(1u << index);
            for (u32& m : raw->matrix)
            {
                m &= clear; // drop the removed group's column from every remaining row
            }
            commit(editedCopy());
            raw->RequestRebuild();
        };
        AddEditor(matrix.Get(), []() {});
    }

    void SceneInspectorView::BuildSettingRow(const TypeInfo* type, const PropertyInfo& prop,
                                             StringView category)
    {
        SceneEditContext* edit = m_edit;
        const StringView name(reinterpret_cast<const utf8char*>(prop.name));
        const bool readOnly =
            (static_cast<u32>(prop.flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0;
        const char* propName = prop.name;

        auto getInstance = [edit, type]() -> Instance
        {
            scene::SceneSystem* system = edit->FindSystemBySettingsType(type);
            return (system != nullptr) ? Instance{system->SettingsInstance(), type} : Instance{};
        };

        // Resource references (the environment's sky texture): the browser-mirroring picker,
        // writing through the settings-flavored ref command.
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::texture::Texture>>())
        {
            BuildSettingResourceRefRow<foundation::texture::Texture>(type, prop, category,
                                                                   {u8"TextureAsset"});
            return;
        }
        // The scene's Level-script reference (SceneScriptSettings::script): the same
        // browser-mirroring picker, filtered to script class assets.
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::script::ScriptClass>>())
        {
            BuildSettingResourceRefRow<foundation::script::ScriptClass>(type, prop, category,
                                                                      {u8"ScriptClassAsset"});
            return;
        }
        auto getVariant = [getInstance, type, propName]() -> Variant
        {
            const Instance settings = getInstance();
            const PropertyInfo* p = settings.IsEmpty() ? nullptr : FindProperty(*type, propName);
            return (p != nullptr) ? GetProperty(*p, settings) : Variant{};
        };

        if (IsEnum(*prop.type))
        {
            const Span<const EnumValue> values = Enumerators(*prop.type);
            Array<StringView> items;
            for (const EnumValue& v : values)
            {
                items.PushBack(StringView(reinterpret_cast<const utf8char*>(v.name)));
            }

            auto rawRead = [getInstance, type, propName]() -> i64
            {
                const Instance settings = getInstance();
                const PropertyInfo* p =
                    settings.IsEmpty() ? nullptr : FindProperty(*type, propName);
                void* address =
                    (p != nullptr && p->address != nullptr) ? p->address(settings) : nullptr;
                if (address == nullptr)
                {
                    return 0;
                }
                switch (p->type->size)
                {
                case 1:
                    return *static_cast<const i8*>(address);
                case 2:
                    return *static_cast<const i16*>(address);
                case 8:
                    return *static_cast<const i64*>(address);
                default:
                    return *static_cast<const i32*>(address);
                }
            };
            auto indexOf = [values](i64 value) -> i32
            {
                for (usize i = 0; i < values.Size(); ++i)
                {
                    if (values[i].value == value)
                    {
                        return static_cast<i32>(i);
                    }
                }
                return 0;
            };
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                DefaultAllocator(), name, indexOf(rawRead()),
                Span<const StringView>{items.Data(), items.Size()},
                readOnly ? Function<void(i32)>{}
                         : Function<void(i32)>{[edit, type, propName, values](i32 index)
                                               {
                                                   if (index >= 0 &&
                                                       index < static_cast<i32>(values.Size()))
                                                   {
                                                       edit->SetSceneSettingPropertyRaw(
                                                           type, propName,
                                                           values[static_cast<usize>(index)].value);
                                                   }
                                               }},
                category);
            AddEditor(editor.Get(), [rawRead, indexOf, raw = editor.Get()]()
                      { raw->SetValue(indexOf(rawRead())); });
            return;
        }

        if (prop.type == &TypeOf<f32>())
        {
            auto value = [getVariant]() -> f64
            {
                const Variant v = getVariant();
                const f32* f = v.TryGet<f32>();
                return (f != nullptr) ? static_cast<f64>(*f) : 0.0;
            };
            // "range" attribute -> bounded slider+field instead of a bare numeric field.
            if (const Float4* range = RangeOf(prop))
            {
                auto editor = MakeRef<ui::toolkit::RangeEditor>(
                    DefaultAllocator(), name, static_cast<f32>(value()), range->x, range->y,
                    range->z,
                    readOnly ? Function<void(f32)>{}
                             : Function<void(f32)>{[edit, type, propName](f32 v)
                                                   {
                                                       edit->SetSceneSettingProperty(
                                                           type, propName, Variant::From<f32>(v));
                                                   }},
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]()
                          { raw->SetValue(static_cast<f32>(value())); });
                return;
            }
            auto editor = MakeRef<ui::toolkit::FloatEditor>(
                DefaultAllocator(), name, value(), -1e9, 1e9, 0.1, 2,
                readOnly ? Function<void(f64)>{}
                         : Function<void(f64)>{[edit, type, propName](f64 v)
                                               {
                                                   edit->SetSceneSettingProperty(
                                                       type, propName,
                                                       Variant::From<f32>(static_cast<f32>(v)));
                                               }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<Color>())
        {
            auto value = [getVariant]() -> Color
            {
                const Variant v = getVariant();
                const Color* c = v.TryGet<Color>();
                return (c != nullptr) ? *c : Color{1, 1, 1, 1};
            };
            auto editor = MakeRef<ui::toolkit::ColorEditor>(
                DefaultAllocator(), name, value(),
                readOnly ? Function<void(Color)>{}
                         : Function<void(Color)>{[edit, type, propName](Color v)
                                                 {
                                                     edit->SetSceneSettingProperty(
                                                         type, propName, Variant::From<Color>(v));
                                                 }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<bool>())
        {
            auto value = [getVariant]() -> bool
            {
                const Variant v = getVariant();
                const bool* b = v.TryGet<bool>();
                return (b != nullptr) && *b;
            };
            auto editor = MakeRef<ui::toolkit::BoolEditor>(
                DefaultAllocator(), name, value(),
                readOnly ? Function<void(bool)>{}
                         : Function<void(bool)>{[edit, type, propName](bool v)
                                                {
                                                    edit->SetSceneSettingProperty(
                                                        type, propName, Variant::From<bool>(v));
                                                }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<Float3>())
        {
            auto value = [getVariant]() -> Float3
            {
                const Variant v = getVariant();
                const Float3* f = v.TryGet<Float3>();
                return (f != nullptr) ? *f : Float3{};
            };
            auto editor = MakeRef<ui::toolkit::Float3Editor>(
                DefaultAllocator(), name, value(), -100000.0f, 100000.0f, 0.1f,
                readOnly ? Function<void(Float3)>{}
                         : Function<void(Float3)>{[edit, type, propName](Float3 v)
                                                  {
                                                      edit->SetSceneSettingProperty(
                                                          type, propName, Variant::From<Float3>(v));
                                                  }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }
        // Other kinds: extend when a settings block needs them.
    }

    namespace
    {
        // User-facing component name: the authored "displayName" type attribute wins;
        // unannotated types fall back to spaced PascalCase minus a trailing "Component"
        // ("ReflectionProbeComponent" -> "Reflection Probe"), so nothing renders raw.
        [[nodiscard]] String ComponentDisplayName(const TypeInfo* type)
        {
            if (const Variant* v = FindAttribute(*type, "displayName"))
            {
                if (const String* s = v->TryGet<String>())
                {
                    return String(s->AsView());
                }
            }
            StringView n(reinterpret_cast<const utf8char*>(type->name));
            const StringView suffix = u8"Component";
            if (n.Size() > suffix.Size() &&
                n.SubStr(n.Size() - suffix.Size(), suffix.Size()) == suffix)
            {
                n = n.SubStr(0, n.Size() - suffix.Size());
            }
            return PrettifyPropertyName(n);
        }

        // Add-menu grouping: the authored "category" type attribute; unannotated types
        // gather under "Other" (the cue that a category is missing, not a design).
        [[nodiscard]] StringView ComponentCategory(const TypeInfo* type)
        {
            if (const Variant* v = FindAttribute(*type, "category"))
            {
                if (const String* s = v->TryGet<String>())
                {
                    return s->AsView();
                }
            }
            return u8"Other";
        }

        // A stable label for a container element: its dynamic type's displayName attribute, else the
        // prettified type name.
        [[nodiscard]] String ContainerElementLabel(const TypeInfo* elementType)
        {
            if (elementType == nullptr)
            {
                return String(u8"(element)");
            }
            const StringView disp =
                TypeAttrString(*elementType, "displayName", StringView{});
            if (!disp.IsEmpty())
            {
                return String(disp);
            }
            return PrettifyPropertyName(StringView(reinterpret_cast<const utf8char*>(elementType->name)));
        }

    }

    // Material-slots UI switch: true = the generic reflection-driven list editor (materials reflected
    // as a container); false = the bespoke mesh-aware editor (BuildMaterialSlots). Both render through
    // the same ContainerListEditor UI. Materials stays reflected either way.
    inline constexpr bool kUseReflectedMaterialSlots = true;

    void SceneInspectorView::BuildComponentSection(const Guid& id, scene::ComponentManagerBase& mgr)
    {
        const TypeInfo* type = mgr.ComponentType();
        if (type == nullptr)
        {
            return;
        }
        // Category = the type name minus a trailing "Component", prettified
        // ("ReflectionProbeComponent" -> "Reflection Probe").
        const StringView fallback = mgr.SerializationTypeId();
        String categoryStorage;
        if (IsRegisteredType(type))
        {
            categoryStorage = ComponentDisplayName(type);
        }
        else
        {
            categoryStorage =
                fallback.IsEmpty() ? StringView(u8"(unreflected component)") : fallback;
        }
        const StringView category = categoryStorage.AsView();

        for (const PropertyInfo& prop : Properties(*type))
        {
            if (prop.type != nullptr && IsContainer(*prop.type))
            {
                // Mesh materials has a bespoke (mesh-aware) editor; when that mode is selected, skip
                // the generic list here and let BuildMaterialSlots render it below.
                const bool bespokeMeshMaterials =
                    !kUseReflectedMaterialSlots &&
                    mgr.SerializationTypeId() == StringView(u8"mesh") &&
                    StringView(reinterpret_cast<const utf8char*>(prop.name)) ==
                        StringView(u8"materials");
                if (!bespokeMeshMaterials)
                {
                    BuildContainerRows(id, type, prop, category); // generic reflected list editor
                }
                continue;
            }
            if (IsNested(prop))
            {
                continue; // nested structures are recursed elsewhere, not a leaf row
            }
            const usize firstRow = m_grid->PropertyCount();
            BuildPropertyRow(id, type, prop, category);
            ApplyPropertyPresentation(type, prop, firstRow,
                                      [edit = m_edit, id, type]() -> Instance
                                      {
                                          scene::ComponentManagerBase* mgr =
                                              edit->FindManager(type);
                                          const scene::EntityHandle e = edit->Resolve(id);
                                          return (mgr != nullptr && e.IsAssigned())
                                                     ? mgr->GetComponentInstance(e)
                                                     : Instance{};
                                      });
        }

        SceneEditContext* edit = m_edit;
        EditorContext* editor = m_editor;

        // RigidBody with a Cooked shape but no collision-shape reference: warn, else the body builds
        // with NO collider (the #7 authoring trap). Advisory only - refreshed when the inspector
        // rebuilds (reselect / structural change).
        if (type == &TypeOf<engine::physics::RigidBodyComponent>())
        {
            const scene::EntityHandle e = edit->Resolve(id);
            auto* bodies = static_cast<engine::physics::RigidBodyComponentManager*>(&mgr);
            engine::physics::RigidBodyComponent* body =
                e.IsAssigned() ? bodies->Get(e) : nullptr;
            if (body != nullptr && body->shape == foundation::physics::ShapeKind::Cooked &&
                body->collisionShape.id.IsNil())
            {
                auto notice = MakeRef<NoticeEditor>(DefaultAllocator(), StringView(u8"Collision"),
                                                    StringView(u8"Physics"));
                notice->message = String(
                    u8"Shape is Cooked but no collision shape is set - this body has no collider. "
                    u8"Assign one (import a mesh with Generate collision, or create a Collision "
                    u8"Shape asset).");
                AddEditor(notice.Get(), []() {});
            }
        }

        // NavMeshZoneComponent: a one-shot Bake button.
        // Collects the in-zone static-mesh geometry, bakes it via Recast, and writes the zone's
        // NavigationZoneAsset sidecar; the outcome is flashed (no silent success). The user then
        // saves + cooks to apply the new navmesh. Requires an assigned Navigation Zone asset.
        if (type == &TypeOf<engine::navigation::NavMeshZoneComponent>())
        {
            auto bake = MakeRef<ui::toolkit::ButtonEditor>(
                DefaultAllocator(), StringView(u8"Bake Navigation"),
                [edit, editor, id]()
                {
                    if (edit == nullptr || editor == nullptr)
                    {
                        return;
                    }
                    const scene::EntityHandle entity = edit->Resolve(id);
                    auto* zones = edit->Scene()
                                      .GetSystem<engine::navigation::NavMeshZoneComponentManager>();
                    engine::navigation::NavMeshZoneComponent* zone =
                        (zones != nullptr && entity.IsAssigned()) ? zones->Get(entity) : nullptr;
                    if (zone == nullptr)
                    {
                        return;
                    }
                    if (editor->Project() == nullptr)
                    {
                        editor->Notify(NoticeKind::Error, u8"No project is open.");
                        return;
                    }
                    foundation::content::Instance* target =
                        zone->zone.id.IsNil()
                            ? nullptr
                            : editor->Project()->SourceDb().GetInstance(zone->zone.id);
                    if (target == nullptr)
                    {
                        editor->Notify(
                            NoticeKind::Warning,
                            u8"Assign a Navigation Zone asset to this zone before baking.");
                        return;
                    }
                    const editor::navigation::BakeResult result =
                        editor::navigation::BakeNavigationZone(edit->Scene(), entity, *target);
                    if (result.baked)
                    {
                        editor->Notify(NoticeKind::Success,
                                       u8"Navigation baked. Save and cook to apply.");
                    }
                    else if (result.triangleCount == 0)
                    {
                        // Nothing was collected: the zone box did not overlap any static mesh.
                        // The bake only gathers Mesh (StaticMesh) geometry whose world bounds
                        // intersect the zone AABB (centered on THIS entity, sized by Extents).
                        editor->Notify(
                            NoticeKind::Warning,
                            u8"No mesh geometry inside the zone box. Check the zone's Extents "
                            u8"cover your floor, that the floor entity has a Mesh component, and "
                            u8"that the zone is placed over it (only static Mesh geometry is "
                            u8"collected).");
                    }
                    else
                    {
                        // Geometry was collected but Recast produced no walkable surface - the
                        // agent/cell parameters did not fit the geometry.
                        editor->Notify(
                            NoticeKind::Warning,
                            Format(u8"Collected {} triangle(s) but Recast produced no walkable "
                                   u8"surface. Try a larger Cell Size or a smaller Agent "
                                   u8"Radius/Height, and check the surface is within Agent Max "
                                   u8"Slope.",
                                   result.triangleCount)
                                .AsView());
                    }
                },
                category);
            AddEditor(bake.Get(), []() {});
        }

        // MeshComponent: the material SLOT list (unified array; slot 0 = whole-mesh). Two UIs, chosen
        // by kUseReflectedMaterialSlots: the bespoke mesh-aware editor (below, renders through the same
        // ContainerListEditor) OR the generic reflection-driven list (already emitted above). Materials
        // is reflected either way (scriptable / tooling-traversable); the flag only picks the inspector
        // UI.
        // `if constexpr` on the flag, then the runtime test: kUseReflectedMaterialSlots is a
        // compile-time switch, and folding it into a runtime && makes cl 19.44 report
        // "C4127: conditional expression is constant" (19.51 does not). This also says what is
        // actually meant - the branch is selected when the code is built, not when it runs.
        if constexpr (!kUseReflectedMaterialSlots)
        {
            if (mgr.SerializationTypeId() == StringView(u8"mesh"))
            {
                BuildMaterialSlots(id, category);
            }
        }

        // ScriptComponent: the ordered behavior list, each a script picker + the
        // rows the cooked ScriptClass metadata drives.
        if (mgr.SerializationTypeId() == StringView(u8"script"))
        {
            BuildScriptBehaviors(id, category);
        }

        // Prefab members: a per-component revert row whose label carries a LIVE override
        // dot (recomputed by the refresher, so it tracks edits and undo without grid
        // rebuilds). Revert rides the undoable paste-component path.
        {
            scene::PrefabMemberInfo member;
            if (mgr.IsSerializable() && scene::FindPrefabMember(edit->Scene(), id, member))
            {
                auto revert = MakeRef<ui::toolkit::ButtonEditor>(
                    DefaultAllocator(), StringView(u8"Revert to Prefab"),
                    Function<void()>{[edit, id, type]()
                                     { (void)edit->RevertComponentToBaseline(id, type); }},
                    category);
                revert->SetTooltip(u8"Reverts this component to the prefab's values (undoable).");
                revert->SetButtonEnabled(false); // refresher enables it on an override
                scene::ComponentManagerBase* manager = &mgr;
                AddEditor(revert.Get(),
                          [edit, id, manager, raw = revert.Get()]()
                          {
                              scene::PrefabMemberInfo m;
                              const bool overridden =
                                  scene::FindPrefabMember(edit->Scene(), id, m) &&
                                  scene::IsPrefabComponentOverridden(edit->Scene(), m, *manager);
                              raw->SetButtonEnabled(overridden);
                              // " *" matches the dirty-tab convention AND stays inside the editor
                              // font's rasterized range (ExtendedLatin = codepoints <= 255; a
                              // U+25CF dot has no glyph and silently renders as nothing).
                              raw->SetDisplayName(overridden ? StringView(u8"Revert to Prefab *")
                                                             : StringView(u8"Revert to Prefab"));
                          });
            }
        }

        // Copy / Remove as right-aligned ICON actions in this component's category header (clicking an
        // icon runs the action; clicking elsewhere on the header toggles the section). Only regular
        // components get these - Transform / scene-settings sections do not add header actions.
        {
            editor::app::EditorIcons& icons = editor::app::EditorIcons::Get();
            auto actions = MakeRef<ui::FlexLayout>(DefaultAllocator());
            actions->Direction = ui::Orientation::Horizontal;
            actions->Spacing = 2.0f;

            auto copyBtn = MakeRef<ui::IconButton>(DefaultAllocator(), icons.copy.Get(), 18.0f);
            copyBtn->TooltipText = String(u8"Copy component");
            copyBtn->OnClick.Add(
                [edit, editor, id, type](ui::ButtonBase*)
                {
                    Array<byte> blob = edit->CopyComponent(id, type);
                    if (!blob.IsEmpty())
                    {
                        editor->SetClipboard(u8"component", Move(blob));
                    }
                });
            actions->AddView(copyBtn.Get());

            auto removeBtn = MakeRef<ui::IconButton>(DefaultAllocator(), icons.remove.Get(), 18.0f);
            removeBtn->TooltipText = String(u8"Remove component");
            removeBtn->OnClick.Add([edit, id, type](ui::ButtonBase*)
                                   { edit->RemoveComponent(id, type); });
            actions->AddView(removeBtn.Get());

            m_grid->SetCategoryHeaderActions(category, RefPtr<ui::View>(actions.Get()));
        }
    }

    i64 SceneInspectorView::RawIntValue(const Instance& obj, const PropertyInfo& p)
    {
        void* address = (p.address != nullptr) ? p.address(obj) : nullptr;
        if (address == nullptr)
        {
            return 0;
        }
        switch (p.type->size)
        {
        case 1:
            return *static_cast<const i8*>(address);
        case 2:
            return *static_cast<const i16*>(address);
        case 8:
            return *static_cast<const i64*>(address);
        default:
            return *static_cast<const i32*>(address);
        }
    }

    void SceneInspectorView::BuildPropertyRow(const Guid& id, const TypeInfo* type,
                                              const PropertyInfo& prop, StringView category)
    {
        SceneEditContext* edit = m_edit;
        const StringView name(reinterpret_cast<const utf8char*>(prop.name));
        const bool readOnly =
            (static_cast<u32>(prop.flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0;
        const char* propName = prop.name;

        // Resource references: a picker over the source DB's matching assets. Matched by
        // EXACT Ref<T> type identity (the TypeInfo pointer), so the unregistered template
        // type name ("<value>") never matters.
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::geometry::StaticMesh>>())
        {
            // SkinnedMeshAsset too: SkinnedMesh IS-A StaticMesh (bind pose when drawn
            // through the static path), so both asset types are valid targets.
            BuildResourceRefRow<foundation::geometry::StaticMesh>(
                id, type, prop, category, {u8"StaticMeshAsset", u8"SkinnedMeshAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::materials::Material>>())
        {
            BuildResourceRefRow<foundation::materials::Material>(id, type, prop, category,
                                                               {u8"MaterialAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::animation::Skeleton>>())
        {
            BuildResourceRefRow<foundation::animation::Skeleton>(id, type, prop, category,
                                                               {u8"SkeletonAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::animation::AnimationClip>>())
        {
            BuildResourceRefRow<foundation::animation::AnimationClip>(id, type, prop, category,
                                                                    {u8"AnimationClipAsset"});
            return;
        }
        if (prop.type ==
            &TypeOf<foundation::resource::Ref<foundation::navigation::NavigationZoneResource>>())
        {
            BuildResourceRefRow<foundation::navigation::NavigationZoneResource>(id, type, prop, category,
                                                                       {u8"NavigationZoneAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::animation::AnimationGraph>>())
        {
            BuildResourceRefRow<foundation::animation::AnimationGraph>(id, type, prop, category,
                                                                     {u8"AnimationGraphAsset"});
            return;
        }
        if (prop.type ==
            &TypeOf<foundation::resource::Ref<
                foundation::propertyanimation::PropertyAnimationClipResource>>())
        {
            BuildResourceRefRow<foundation::propertyanimation::PropertyAnimationClipResource>(
                id, type, prop, category, {u8"PropertyAnimationClipAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::texture::Texture>>())
        {
            BuildResourceRefRow<foundation::texture::Texture>(id, type, prop, category,
                                                            {u8"TextureAsset"});
            return;
        }
        if (prop.type ==
            &TypeOf<foundation::resource::Ref<foundation::particles::ParticleEffectResource>>())
        {
            BuildResourceRefRow<foundation::particles::ParticleEffectResource>(
                id, type, prop, category, {u8"ParticleEffectAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::physics::CollisionShape>>())
        {
            BuildResourceRefRow<foundation::physics::CollisionShape>(id, type, prop, category,
                                                                   {u8"CollisionShapeAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::physics::PhysicalMaterial>>())
        {
            BuildResourceRefRow<foundation::physics::PhysicalMaterial>(id, type, prop, category,
                                                                     {u8"PhysicalMaterialAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::heightfield::Heightfield>>())
        {
            BuildResourceRefRow<foundation::heightfield::Heightfield>(id, type, prop, category,
                                                                    {u8"HeightfieldAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::terrain::TerrainResource>>())
        {
            BuildResourceRefRow<foundation::terrain::TerrainResource>(id, type, prop, category,
                                                                    {u8"TerrainAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::audio::AudioClip>>())
        {
            BuildResourceRefRow<foundation::audio::AudioClip>(id, type, prop, category,
                                                            {u8"AudioClipAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::audio::SoundCue>>())
        {
            BuildResourceRefRow<foundation::audio::SoundCue>(id, type, prop, category,
                                                           {u8"SoundCueAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::ui::UIDocument>>())
        {
            BuildResourceRefRow<foundation::ui::UIDocument>(id, type, prop, category,
                                                          {u8"UIDocumentAsset"});
            return;
        }
        if (prop.type == &TypeOf<foundation::resource::Ref<foundation::ui::UITheme>>())
        {
            BuildResourceRefRow<foundation::ui::UITheme>(id, type, prop, category,
                                                       {u8"UIThemeAsset"});
            return;
        }
        // Entity reference: a picker over the CURRENT scene's entities (the typed EntityRef field -
        // e.g. JointComponent::targetEntity). A bare Guid would have no editor here.
        if (prop.type == &TypeOf<foundation::scene::EntityRef>())
        {
            BuildEntityRefRow(id, type, prop, category);
            return;
        }

        // Pulls the current Variant (empty component -> default Variant guards below).
        auto getVariant = [edit, id, type, propName]() -> Variant
        {
            scene::ComponentManagerBase* mgr = edit->FindManager(type);
            const scene::EntityHandle e = edit->Resolve(id);
            if (mgr == nullptr || !e.IsAssigned())
            {
                return {};
            }
            const Instance component = mgr->GetComponentInstance(e);
            const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
            return (p != nullptr) ? GetProperty(*p, component) : Variant{};
        };

        if (IsEnum(*prop.type))
        {
            const Span<const EnumValue> values = Enumerators(*prop.type);
            Array<StringView> items;
            for (const EnumValue& v : values)
            {
                items.PushBack(StringView(reinterpret_cast<const utf8char*>(v.name)));
            }

            auto rawRead = [edit, id, type, propName]() -> i64
            {
                scene::ComponentManagerBase* mgr = edit->FindManager(type);
                const scene::EntityHandle e = edit->Resolve(id);
                if (mgr == nullptr || !e.IsAssigned())
                {
                    return 0;
                }
                const Instance component = mgr->GetComponentInstance(e);
                const PropertyInfo* p =
                    component.IsEmpty() ? nullptr : FindProperty(*type, propName);
                void* address =
                    (p != nullptr && p->address != nullptr) ? p->address(component) : nullptr;
                if (address == nullptr)
                {
                    return 0;
                }
                switch (p->type->size)
                {
                case 1:
                    return *static_cast<const i8*>(address);
                case 2:
                    return *static_cast<const i16*>(address);
                case 8:
                    return *static_cast<const i64*>(address);
                default:
                    return *static_cast<const i32*>(address);
                }
            };
            auto indexOf = [values](i64 value) -> i32
            {
                for (usize i = 0; i < values.Size(); ++i)
                {
                    if (values[i].value == value)
                    {
                        return static_cast<i32>(i);
                    }
                }
                return 0;
            };

            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                DefaultAllocator(), name, indexOf(rawRead()),
                Span<const StringView>{items.Data(), items.Size()},
                readOnly ? Function<void(i32)>{}
                         : Function<void(i32)>{[edit, id, type, propName, values](i32 index)
                                               {
                                                   if (index >= 0 &&
                                                       index < static_cast<i32>(values.Size()))
                                                   {
                                                       edit->SetComponentPropertyRaw(
                                                           id, type, propName,
                                                           values[static_cast<usize>(index)].value);
                                                   }
                                               }},
                category);
            AddEditor(editor.Get(), [rawRead, indexOf, raw = editor.Get()]()
                      { raw->SetValue(indexOf(rawRead())); });
            return;
        }

        if (prop.type == &TypeOf<f32>())
        {
            auto value = [getVariant]() -> f64
            {
                const Variant v = getVariant();
                const f32* f = v.TryGet<f32>();
                return (f != nullptr) ? static_cast<f64>(*f) : 0.0;
            };
            // "range" attribute -> bounded slider+field instead of a bare numeric field.
            if (const Float4* range = RangeOf(prop))
            {
                auto editor = MakeRef<ui::toolkit::RangeEditor>(
                    DefaultAllocator(), name, static_cast<f32>(value()), range->x, range->y,
                    range->z,
                    readOnly
                        ? Function<void(f32)>{}
                        : Function<void(f32)>{[edit, id, type, propName](f32 v)
                                              {
                                                  edit->SetComponentProperty(id, type, propName,
                                                                             Variant::From<f32>(v));
                                              }},
                    category);
                AddEditor(editor.Get(), [value, raw = editor.Get()]()
                          { raw->SetValue(static_cast<f32>(value())); });
                return;
            }
            auto editor = MakeRef<ui::toolkit::FloatEditor>(
                DefaultAllocator(), name, value(), -1e9, 1e9, 0.1, 2,
                readOnly ? Function<void(f64)>{}
                         : Function<void(f64)>{[edit, id, type, propName](f64 v)
                                               {
                                                   edit->SetComponentProperty(
                                                       id, type, propName,
                                                       Variant::From<f32>(static_cast<f32>(v)));
                                               }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<bool>())
        {
            auto value = [getVariant]() -> bool
            {
                const Variant v = getVariant();
                const bool* b = v.TryGet<bool>();
                return b != nullptr && *b;
            };
            auto editor = MakeRef<ui::toolkit::BoolEditor>(
                DefaultAllocator(), name, value(),
                readOnly ? Function<void(bool)>{}
                         : Function<void(bool)>{[edit, id, type, propName](bool v)
                                                {
                                                    edit->SetComponentProperty(
                                                        id, type, propName, Variant::From<bool>(v));
                                                }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        // Every integer width, narrow ones INCLUDED: a u8 field (e.g. RigidBodyComponent.collisionGroup)
        // is reflected but was invisible when only i32/u32/i64/u64 were handled - it silently fell
        // through with no editor. All widths edit through one i64-backed IntEditor.
        if (prop.type == &TypeOf<i8>() || prop.type == &TypeOf<u8>() || prop.type == &TypeOf<i16>() ||
            prop.type == &TypeOf<u16>() || prop.type == &TypeOf<i32>() ||
            prop.type == &TypeOf<u32>() || prop.type == &TypeOf<i64>() ||
            prop.type == &TypeOf<u64>())
        {
            const TypeInfo* intType = prop.type;
            auto value = [getVariant, intType]() -> i64
            {
                const Variant v = getVariant();
                if (intType == &TypeOf<i8>())
                {
                    const i8* p = v.TryGet<i8>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                if (intType == &TypeOf<u8>())
                {
                    const u8* p = v.TryGet<u8>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                if (intType == &TypeOf<i16>())
                {
                    const i16* p = v.TryGet<i16>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                if (intType == &TypeOf<u16>())
                {
                    const u16* p = v.TryGet<u16>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                if (intType == &TypeOf<i32>())
                {
                    const i32* p = v.TryGet<i32>();
                    return p ? *p : 0;
                }
                if (intType == &TypeOf<u32>())
                {
                    const u32* p = v.TryGet<u32>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                if (intType == &TypeOf<u64>())
                {
                    const u64* p = v.TryGet<u64>();
                    return p ? static_cast<i64>(*p) : 0;
                }
                const i64* p = v.TryGet<i64>();
                return p ? *p : 0;
            };
            auto setter = [edit, id, type, propName, intType](i64 v)
            {
                if (intType == &TypeOf<i8>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<i8>(static_cast<i8>(v)));
                }
                else if (intType == &TypeOf<u8>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<u8>(static_cast<u8>(v)));
                }
                else if (intType == &TypeOf<i16>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<i16>(static_cast<i16>(v)));
                }
                else if (intType == &TypeOf<u16>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<u16>(static_cast<u16>(v)));
                }
                else if (intType == &TypeOf<i32>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<i32>(static_cast<i32>(v)));
                }
                else if (intType == &TypeOf<u32>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<u32>(static_cast<u32>(v)));
                }
                else if (intType == &TypeOf<u64>())
                {
                    edit->SetComponentProperty(id, type, propName,
                                               Variant::From<u64>(static_cast<u64>(v)));
                }
                else
                {
                    edit->SetComponentProperty(id, type, propName, Variant::From<i64>(v));
                }
            };
            auto editor = MakeRef<ui::toolkit::IntEditor>(
                DefaultAllocator(), name, value(), std::numeric_limits<i64>::min(),
                std::numeric_limits<i64>::max(),
                readOnly ? Function<void(i64)>{} : Function<void(i64)>{Move(setter)}, category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<String>())
        {
            auto value = [getVariant]() -> String
            {
                const Variant v = getVariant();
                const String* s = v.TryGet<String>();
                return (s != nullptr) ? String(*s) : String{};
            };
            auto editor = MakeRef<ui::toolkit::StringEditor>(
                DefaultAllocator(), name, value().AsView(),
                readOnly ? Function<void(StringView)>{}
                         : Function<void(StringView)>{[edit, id, type, propName](StringView v)
                                                      {
                                                          edit->SetComponentProperty(
                                                              id, type, propName,
                                                              Variant::From<String>(String(v)));
                                                      }},
                category);
            AddEditor(editor.Get(),
                      [value, raw = editor.Get()]() { raw->SetValue(value().AsView()); });
            return;
        }

        if (prop.type == &TypeOf<Float3>())
        {
            auto value = [getVariant]() -> Float3
            {
                const Variant v = getVariant();
                const Float3* f = v.TryGet<Float3>();
                return (f != nullptr) ? *f : Float3{};
            };
            auto editor = MakeRef<ui::toolkit::Float3Editor>(
                DefaultAllocator(), name, value(), -100000.0f, 100000.0f, 0.1f,
                readOnly
                    ? Function<void(Float3)>{}
                    : Function<void(Float3)>{[edit, id, type, propName](Float3 v)
                                             {
                                                 edit->SetComponentProperty(
                                                     id, type, propName, Variant::From<Float3>(v));
                                             }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<Color>())
        {
            auto value = [getVariant]() -> Color
            {
                const Variant v = getVariant();
                const Color* c = v.TryGet<Color>();
                return (c != nullptr) ? *c : Color{1, 1, 1, 1};
            };
            auto editor = MakeRef<ui::toolkit::ColorEditor>(
                DefaultAllocator(), name, value(),
                readOnly
                    ? Function<void(Color)>{}
                    : Function<void(Color)>{[edit, id, type, propName](Color v)
                                            {
                                                edit->SetComponentProperty(id, type, propName,
                                                                           Variant::From<Color>(v));
                                            }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<Float2>())
        {
            auto value = [getVariant]() -> Float2
            {
                const Variant v = getVariant();
                const Float2* f = v.TryGet<Float2>();
                return (f != nullptr) ? *f : Float2{};
            };
            auto editor = MakeRef<ui::toolkit::Float2Editor>(
                DefaultAllocator(), name, value(), -100000.0f, 100000.0f, 0.1f,
                readOnly
                    ? Function<void(Float2)>{}
                    : Function<void(Float2)>{[edit, id, type, propName](Float2 v)
                                             {
                                                 edit->SetComponentProperty(
                                                     id, type, propName, Variant::From<Float2>(v));
                                             }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        if (prop.type == &TypeOf<Float4>())
        {
            auto value = [getVariant]() -> Float4
            {
                const Variant v = getVariant();
                const Float4* f = v.TryGet<Float4>();
                return (f != nullptr) ? *f : Float4{};
            };
            auto editor = MakeRef<ui::toolkit::Float4Editor>(
                DefaultAllocator(), name, value(), -100000.0f, 100000.0f, 0.1f,
                readOnly
                    ? Function<void(Float4)>{}
                    : Function<void(Float4)>{[edit, id, type, propName](Float4 v)
                                             {
                                                 edit->SetComponentProperty(
                                                     id, type, propName, Variant::From<Float4>(v));
                                             }},
                category);
            AddEditor(editor.Get(), [value, raw = editor.Get()]() { raw->SetValue(value()); });
            return;
        }

        // Unsupported reflected type: skipped.
    }

    void SceneInspectorView::MutateMeshMaterials(
        const Guid& id, const Function<void(engine::render::MeshComponent&)>& mutate)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        auto* manager = m_edit->Scene().GetSystem<engine::render::MeshComponentManager>();
        engine::render::MeshComponent* live =
            (manager != nullptr && e.IsAssigned()) ? manager->Get(e) : nullptr;
        if (live == nullptr)
        {
            return;
        }
        const engine::render::MeshComponent before = *live;
        mutate(*live);
        Array<byte> blob = m_edit->CopyComponent(id, &TypeOf<engine::render::MeshComponent>());
        *live = before;
        if (!blob.IsEmpty())
        {
            (void)m_edit->PasteComponent(id, Span<const byte>{blob.Data(), blob.Size()});
        }
    }

    Array<String> SceneInspectorView::MaterialSlotNames(const engine::render::MeshComponent& mc)
    {
        Array<String> names;
        for (usize i = 0; i < mc.materials.Size(); ++i)
        {
            const Guid target = mc.materials[i].id;
            if (!target.IsNil())
            {
                names.PushBack(String(AssetNameFor(target)));
            }
            else if (mc.materials[i].Get() != nullptr)
            {
                names.PushBack(String(u8"(runtime)"));
            }
            else
            {
                names.PushBack(String(u8"(none)"));
            }
        }
        return names;
    }

    void SceneInspectorView::BuildMaterialSlots(const Guid& id, StringView category)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        auto* manager = m_edit->Scene().GetSystem<engine::render::MeshComponentManager>();
        engine::render::MeshComponent* mc =
            (manager != nullptr && e.IsAssigned()) ? manager->Get(e) : nullptr;
        if (mc == nullptr)
        {
            return;
        }

        auto slots =
            MakeRef<ContainerListEditor>(DefaultAllocator(), StringView(u8"Materials"), category);
        slots->SetTooltip(u8"Material slots, indexed by the mesh's submesh material index. "
                          u8"Slot 0 also covers single-material meshes and any submesh "
                          u8"whose index has no slot.");
        slots->slotNames = MaterialSlotNames(*mc);

        SceneInspectorView* self = this;
        slots->OnAdd = [self, id]()
        {
            self->MutateMeshMaterials(
                id,
                [](engine::render::MeshComponent& c)
                {
                    c.materials.PushBack(foundation::resource::Ref<foundation::materials::Material>{});
                });
        };
        slots->OnRemoveSlot = [self, id](usize slot)
        {
            self->MutateMeshMaterials(id,
                                      [slot](engine::render::MeshComponent& c)
                                      {
                                          if (slot < c.materials.Size())
                                          {
                                              c.materials.RemoveAt(slot);
                                          }
                                      });
        };
        slots->OnMoveSlot = [self, id](usize slot, bool up)
        {
            self->MutateMeshMaterials(
                id,
                [slot, up](engine::render::MeshComponent& c)
                {
                    const usize other = up ? slot - 1 : slot + 1;
                    if (slot < c.materials.Size() && other < c.materials.Size())
                    {
                        foundation::resource::Ref<foundation::materials::Material> tmp =
                            c.materials[slot];
                        c.materials[slot] = c.materials[other];
                        c.materials[other] = tmp;
                    }
                });
        };
        slots->OnPickSlot = [self, id](usize slot)
        {
            if (self->Context == nullptr || self->m_editor->Project() == nullptr)
            {
                return;
            }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"MaterialAsset"));
            auto picker = MakeRef<editor::app::AssetPickerDialog>(
                DefaultAllocator(), *self->m_editor, Move(typeNames));
            picker->OnPicked = [self, id, slot](const Guid& picked)
            {
                self->MutateMeshMaterials(
                    id,
                    [slot, picked](engine::render::MeshComponent& c)
                    {
                        if (slot >= c.materials.Size())
                        {
                            return;
                        }
                        c.materials[slot] =
                            foundation::resource::Ref<foundation::materials::Material>{};
                        c.materials[slot].SetId(picked);
                    });
            };
            picker->Show(self->Context);
        };
        RefPtr<ContainerListEditor> slotsRef = slots;
        AddEditor(slots.Get(),
                  [self, id, slotsRef]()
                  {
                      const scene::EntityHandle live = self->m_edit->Resolve(id);
                      auto* mgr =
                          self->m_edit->Scene().GetSystem<engine::render::MeshComponentManager>();
                      engine::render::MeshComponent* c =
                          (mgr != nullptr && live.IsAssigned()) ? mgr->Get(live) : nullptr;
                      if (c == nullptr)
                      {
                          return;
                      } // presence loss flips the Signature anyway
                      const Array<String> names = self->MaterialSlotNames(*c);
                      bool same = names.Size() == slotsRef->slotNames.Size();
                      for (usize i = 0; same && i < names.Size(); ++i)
                      {
                          same = names[i] == slotsRef->slotNames[i];
                      }
                      if (!same)
                      {
                          self->m_forceRebuild = true;
                      }
                  });
    }

    void SceneInspectorView::MutateScriptComponent(
        const Guid& id, const Function<void(engine::script::ScriptComponent&)>& mutate)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        auto* manager = m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
        engine::script::ScriptComponent* live =
            (manager != nullptr && e.IsAssigned()) ? manager->Get(e) : nullptr;
        if (live == nullptr)
        {
            return;
        }
        const engine::script::ScriptComponent before = *live;
        mutate(*live);
        Array<byte> blob = m_edit->CopyComponent(id, &TypeOf<engine::script::ScriptComponent>());
        *live = before;
        if (!blob.IsEmpty())
        {
            (void)m_edit->PasteComponent(id, Span<const byte>{blob.Data(), blob.Size()});
        }
    }

    foundation::script::ScriptClass*
    SceneInspectorView::BehaviorClass(const engine::script::ScriptBehavior& behavior)
    {
        if (behavior.script.Get() != nullptr)
        {
            return behavior.script.Get();
        }
        if (behavior.script.id.IsNil() || m_editor->Resources() == nullptr)
        {
            return nullptr;
        }
        auto proxy = m_editor->Resources()->Bind<foundation::script::ScriptClass>(behavior.script.id);
        return proxy.Get();
    }

    u64 SceneInspectorView::ScriptBehaviorsSignature(const engine::script::ScriptComponent& c)
    {
        u64 hash = HashInteger(c.behaviors.Size());
        for (const engine::script::ScriptBehavior& b : c.behaviors)
        {
            hash = HashBytes(&b.script.id, sizeof(Guid), hash);
            const u64 flags = (b.enabled ? 1u : 0u) | (b.overrides.Size() << 1);
            hash = HashBytes(&flags, sizeof(flags), hash);
        }
        return hash;
    }

    void SceneInspectorView::BuildScriptBehaviors(const Guid& id, StringView category)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        auto* manager = m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
        engine::script::ScriptComponent* component =
            (manager != nullptr && e.IsAssigned()) ? manager->Get(e) : nullptr;
        if (component == nullptr)
        {
            return;
        }

        SceneInspectorView* self = this;
        for (usize i = 0; i < component->behaviors.Size(); ++i)
        {
            BuildScriptBehaviorRows(id, category, i);
        }

        auto add = MakeRef<ui::toolkit::ButtonEditor>(
            DefaultAllocator(), StringView(u8"+ Add Behavior"),
            Function<void()>{[self, id]()
                             {
                                 self->MutateScriptComponent(
                                     id, [](engine::script::ScriptComponent& c)
                                     { c.behaviors.PushBack(engine::script::ScriptBehavior{}); });
                             }},
            category);
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(add.Get()));

        // Shape-change watcher (add/remove/reorder/pick/override toggle rebuilds).
        const u64 signature = ScriptBehaviorsSignature(*component);
        auto watcher = MakeRef<ui::toolkit::ButtonEditor>(DefaultAllocator(), StringView(u8""),
                                                          Function<void()>{[]() {}}, category);
        watcher->SetRowVisible(false);
        AddEditor(
            watcher.Get(),
            [self, id, signature]()
            {
                const scene::EntityHandle live = self->m_edit->Resolve(id);
                auto* mgr =
                    self->m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
                engine::script::ScriptComponent* c =
                    (mgr != nullptr && live.IsAssigned()) ? mgr->Get(live) : nullptr;
                if (c != nullptr && self->ScriptBehaviorsSignature(*c) != signature)
                {
                    self->m_forceRebuild = true;
                }
            });
    }

    void SceneInspectorView::BuildScriptBehaviorRows(const Guid& id, StringView category,
                                                     usize index)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        auto* manager = m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
        engine::script::ScriptComponent* component =
            (manager != nullptr && e.IsAssigned()) ? manager->Get(e) : nullptr;
        if (component == nullptr || index >= component->behaviors.Size())
        {
            return;
        }
        engine::script::ScriptBehavior& behavior = component->behaviors[index];
        SceneInspectorView* self = this;

        // Script picker (AssetPickerDialog filtered to ScriptClass).
        const StringView assetName =
            behavior.script.id.IsNil() ? StringView(u8"(none)") : AssetNameFor(behavior.script.id);
        auto picker = MakeRef<ResourceRefEditor>(DefaultAllocator(), StringView(u8"Script"),
                                                 assetName, category);
        ResourceRefEditor* pickerRaw = picker.Get();
        pickerRaw->OnPick = [self, id, index]()
        {
            if (self->Context == nullptr || self->m_editor->Project() == nullptr)
            {
                return;
            }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"ScriptClassAsset"));
            auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                DefaultAllocator(), *self->m_editor, Move(typeNames));
            dialog->OnPicked = [self, id, index](const Guid& picked)
            {
                self->MutateScriptComponent(
                    id,
                    [index, picked](engine::script::ScriptComponent& c)
                    {
                        if (index >= c.behaviors.Size())
                        {
                            return;
                        }
                        c.behaviors[index].script =
                            foundation::resource::Ref<foundation::script::ScriptClass>{};
                        c.behaviors[index].script.SetId(picked);
                        c.behaviors[index].overrides.Clear(); // metadata changed
                    });
            };
            dialog->Show(self->Context);
        };
        AddEditor(
            pickerRaw,
            [self, id, index, pickerRaw]()
            {
                const scene::EntityHandle live = self->m_edit->Resolve(id);
                auto* mgr =
                    self->m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
                engine::script::ScriptComponent* c =
                    (mgr != nullptr && live.IsAssigned()) ? mgr->Get(live) : nullptr;
                if (c == nullptr || index >= c->behaviors.Size())
                {
                    return;
                }
                const Guid target = c->behaviors[index].script.id;
                pickerRaw->SetValueText(target.IsNil() ? StringView(u8"(none)")
                                                       : self->AssetNameFor(target));
            });

        // Enabled toggle.
        auto enabled = MakeRef<ui::toolkit::BoolEditor>(
            DefaultAllocator(), StringView(u8"Enabled"), behavior.enabled,
            Function<void(bool)>{[self, id, index](bool value)
                                 {
                                     self->MutateScriptComponent(
                                         id,
                                         [index, value](engine::script::ScriptComponent& c)
                                         {
                                             if (index < c.behaviors.Size())
                                             {
                                                 c.behaviors[index].enabled = value;
                                             }
                                         });
                                 }},
            category);
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(enabled.Get()));

        // Update interval (throttling): seconds between onUpdate; 0 = every tick.
        auto interval = MakeRef<ui::toolkit::FloatEditor>(
            DefaultAllocator(), StringView(u8"Update Interval"),
            static_cast<f64>(behavior.updateInterval), 0.0, 3600.0, 0.05, 3,
            Function<void(f64)>{[self, id, index](f64 value)
                                {
                                    self->MutateScriptComponent(
                                        id,
                                        [index, value](engine::script::ScriptComponent& c)
                                        {
                                            if (index < c.behaviors.Size())
                                            {
                                                c.behaviors[index].updateInterval =
                                                    static_cast<f32>(value < 0.0 ? 0.0 : value);
                                            }
                                        });
                                }},
            category);
        interval->SetTooltip(StringView(u8"Seconds between onUpdate calls (0 = every frame)"));
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(interval.Get()));

        // Reorder / remove.
        auto up = MakeRef<ui::toolkit::ButtonEditor>(
            DefaultAllocator(), StringView(u8"Move Up"),
            Function<void()>{[self, id, index]()
                             {
                                 self->MutateScriptComponent(
                                     id,
                                     [index](engine::script::ScriptComponent& c)
                                     {
                                         if (index > 0 && index < c.behaviors.Size())
                                         {
                                             engine::script::ScriptBehavior tmp =
                                                 Move(c.behaviors[index]);
                                             c.behaviors[index] = Move(c.behaviors[index - 1]);
                                             c.behaviors[index - 1] = Move(tmp);
                                         }
                                     });
                             }},
            category);
        up->SetButtonEnabled(index > 0);
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(up.Get()));
        auto remove = MakeRef<ui::toolkit::ButtonEditor>(
            DefaultAllocator(), StringView(u8"Remove Behavior"),
            Function<void()>{[self, id, index]()
                             {
                                 self->MutateScriptComponent(
                                     id,
                                     [index](engine::script::ScriptComponent& c)
                                     {
                                         if (index < c.behaviors.Size())
                                         {
                                             c.behaviors.RemoveAt(index);
                                         }
                                     });
                             }},
            category);
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(remove.Get()));

        // Property rows from the cooked ScriptClass metadata (data-driven; no VM).
        foundation::script::ScriptClass* scriptClass = BehaviorClass(behavior);
        if (scriptClass == nullptr)
        {
            return;
        }
        for (const foundation::script::ScriptPropertyDesc& property : scriptClass->properties)
        {
            const u64 hash = property.hash;
            using engine::script::ScriptComponent;
            using foundation::script::ScriptPropertyValue;
            auto access = MakeRef<ScriptPropertyAccess>(DefaultAllocator());
            // The effective value re-resolves the component each call (pools move on edit).
            access->effective = [self, id, index, hash, property]() -> ScriptPropertyValue
            {
                const scene::EntityHandle live = self->m_edit->Resolve(id);
                auto* mgr =
                    self->m_edit->Scene().GetSystem<engine::script::ScriptComponentManager>();
                ScriptComponent* c = (mgr != nullptr && live.IsAssigned()) ? mgr->Get(live) : nullptr;
                if (c != nullptr && index < c->behaviors.Size())
                {
                    if (const auto* over = c->behaviors[index].FindOverride(hash))
                    {
                        return over->value;
                    }
                }
                return property.defaultValue;
            };
            access->setOverride = [self, id, index, hash](const ScriptPropertyValue& value)
            {
                self->MutateScriptComponent(id,
                                            [index, hash, value](ScriptComponent& c)
                                            {
                                                if (index < c.behaviors.Size())
                                                {
                                                    c.behaviors[index].SetOverride(hash, value);
                                                }
                                            });
            };
            access->removeOverride = [self, id, index, hash]()
            {
                self->MutateScriptComponent(id,
                                            [index, hash](ScriptComponent& c)
                                            {
                                                if (index < c.behaviors.Size())
                                                {
                                                    c.behaviors[index].RemoveOverride(hash);
                                                }
                                            });
            };
            BuildScriptPropertyRow(category, property, access);
        }
    }

    void
    SceneInspectorView::BuildScriptPropertyRow(StringView category,
                                               const foundation::script::ScriptPropertyDesc& property,
                                               const RefPtr<ScriptPropertyAccess>& access)
    {
        using foundation::script::ScriptPropertyType;
        using foundation::script::ScriptPropertyValue;

        // The access holder is shared (by RefPtr copy) into every editor + refresh closure, so the
        // move-only read/write hooks outlive this call: access->effective() reads, ->setOverride writes.
        const StringView name = property.name.AsView();

        switch (property.type)
        {
        case ScriptPropertyType::Float:
        {
            auto editor = MakeRef<ui::toolkit::FloatEditor>(
                DefaultAllocator(), name, access->effective().number, -1e9, 1e9, 0.1, 3,
                Function<void(f64)>{[access](f64 v)
                                    {
                                        ScriptPropertyValue value;
                                        value.kind = ScriptPropertyType::Float;
                                        value.number = v;
                                        access->setOverride(value);
                                    }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(),
                      [access, raw = editor.Get()]() { raw->SetValue(access->effective().number); });
            break;
        }
        case ScriptPropertyType::Int:
        {
            auto editor = MakeRef<ui::toolkit::IntEditor>(
                DefaultAllocator(), name, static_cast<i64>(access->effective().number),
                std::numeric_limits<i64>::min(), std::numeric_limits<i64>::max(),
                Function<void(i64)>{[access](i64 v)
                                    {
                                        ScriptPropertyValue value;
                                        value.kind = ScriptPropertyType::Int;
                                        value.number = static_cast<f64>(v);
                                        access->setOverride(value);
                                    }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(), [access, raw = editor.Get()]()
                      { raw->SetValue(static_cast<i64>(access->effective().number)); });
            break;
        }
        case ScriptPropertyType::Bool:
        {
            auto editor = MakeRef<ui::toolkit::BoolEditor>(
                DefaultAllocator(), name, access->effective().boolean,
                Function<void(bool)>{[access](bool v)
                                     {
                                         ScriptPropertyValue value;
                                         value.kind = ScriptPropertyType::Bool;
                                         value.boolean = v;
                                         access->setOverride(value);
                                     }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(),
                      [access, raw = editor.Get()]() { raw->SetValue(access->effective().boolean); });
            break;
        }
        case ScriptPropertyType::String:
        {
            auto editor = MakeRef<ui::toolkit::StringEditor>(
                DefaultAllocator(), name, access->effective().text.AsView(),
                Function<void(StringView)>{[access](StringView v)
                                           {
                                               ScriptPropertyValue value;
                                               value.kind = ScriptPropertyType::String;
                                               value.text = String(v);
                                               access->setOverride(value);
                                           }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(), [access, raw = editor.Get()]()
                      { raw->SetValue(access->effective().text.AsView()); });
            break;
        }
        case ScriptPropertyType::Color:
        {
            auto editor = MakeRef<ui::toolkit::ColorEditor>(
                DefaultAllocator(), name, access->effective().color,
                Function<void(Color)>{[access](Color v)
                                      {
                                          ScriptPropertyValue value;
                                          value.kind = ScriptPropertyType::Color;
                                          value.color = v;
                                          access->setOverride(value);
                                      }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(),
                      [access, raw = editor.Get()]() { raw->SetValue(access->effective().color); });
            break;
        }
        case ScriptPropertyType::Vec3:
        {
            auto editor = MakeRef<ui::toolkit::Float3Editor>(
                DefaultAllocator(), name, access->effective().vector, -1e9f, 1e9f, 0.1f,
                Function<void(Float3)>{[access](Float3 v)
                                       {
                                           ScriptPropertyValue value;
                                           value.kind = ScriptPropertyType::Vec3;
                                           value.vector = v;
                                           access->setOverride(value);
                                       }},
                category);
            if (!property.description.IsEmpty())
            {
                editor->SetTooltip(property.description.AsView());
            }
            AddEditor(editor.Get(),
                      [access, raw = editor.Get()]() { raw->SetValue(access->effective().vector); });
            break;
        }
        case ScriptPropertyType::Entity:
        {
            BuildScriptEntityPropertyRow(category, property, access);
            break;
        }
        case ScriptPropertyType::Asset:
        {
            BuildScriptAssetPropertyRow(category, property, access);
            break;
        }
        case ScriptPropertyType::None:
        default:
            break;
        }
    }

    void SceneInspectorView::BuildScriptEntityPropertyRow(
        StringView category, const foundation::script::ScriptPropertyDesc& property,
        const RefPtr<ScriptPropertyAccess>& access)
    {
        using foundation::script::ScriptPropertyType;
        using foundation::script::ScriptPropertyValue;
        SceneInspectorView* self = this;

        auto currentTarget = [access]() -> Guid { return access->effective().guid; };
        auto nameOf = [self](const Guid& target) -> StringView
        {
            if (target.IsNil())
            {
                return u8"(none)";
            }
            const scene::EntityHandle h = self->m_edit->Scene().FindEntity(target);
            return h.IsAssigned() ? self->m_edit->Scene().GetEntityName(h)
                                  : StringView(u8"(missing)");
        };

        auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), property.name.AsView(),
                                                 nameOf(currentTarget()), category);
        ResourceRefEditor* raw = editor.Get();
        if (!property.description.IsEmpty())
        {
            raw->SetTooltip(property.description.AsView());
        }
        raw->OnPick = [self, access]()
        {
            if (self->Context == nullptr)
            {
                return;
            }
            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
            menu->AddItem(StringView(u8"(none)"), [access]() { access->removeOverride(); });
            menu->AddSeparator();
            self->m_edit->Scene().ForEachEntity(
                [self, access, &menu](scene::EntityHandle handle)
                {
                    const Guid target = self->m_edit->Scene().GetEntityId(handle);
                    String label(self->m_edit->Scene().GetEntityName(handle));
                    menu->AddItem(label.AsView(),
                                  [access, target]()
                                  {
                                      ScriptPropertyValue value;
                                      value.kind = ScriptPropertyType::Entity;
                                      value.guid = target;
                                      access->setOverride(value);
                                  });
                });
            const Float2 pos = self->m_addButton->LocalToScreen(Float2{0.0f, 0.0f});
            menu->Show(self->Context, pos.x, pos.y);
        };
        AddEditor(raw,
                  [self, currentTarget, nameOf, raw]()
                  {
                      (void)self;
                      raw->SetValueText(nameOf(currentTarget()));
                  });
    }

    // Reflected-component EntityRef picker: the twin of BuildScriptEntityPropertyRow (which serves
    // SCRIPT entity properties), but reading/writing a reflected component field via its property
    // address + an undoable SetComponentEntityRef.
    void SceneInspectorView::BuildEntityRefRow(const Guid& id, const TypeInfo* type,
                                               const PropertyInfo& prop, StringView category)
    {
        SceneInspectorView* self = this;
        SceneEditContext* edit = m_edit;
        const char* propName = prop.name;
        const StringView name(reinterpret_cast<const utf8char*>(prop.name));

        // Current EntityRef.id via the re-derived property address (component pools move).
        auto currentTarget = [edit, id, type, propName]() -> Guid
        {
            scene::ComponentManagerBase* mgr = edit->FindManager(type);
            const scene::EntityHandle e = edit->Resolve(id);
            if (mgr == nullptr || !e.IsAssigned())
            {
                return Guid{};
            }
            const Instance component = mgr->GetComponentInstance(e);
            const PropertyInfo* p = component.IsEmpty() ? nullptr : FindProperty(*type, propName);
            void* address =
                (p != nullptr && p->address != nullptr) ? p->address(component) : nullptr;
            return (address != nullptr) ? static_cast<foundation::scene::EntityRef*>(address)->id
                                        : Guid{};
        };
        // Display name of a target guid (scene-owned strings / literals - safe to hold as a view).
        auto nameOf = [edit](const Guid& target) -> StringView
        {
            if (target.IsNil())
            {
                return u8"(none)";
            }
            const scene::EntityHandle h = edit->Scene().FindEntity(target);
            return h.IsAssigned() ? edit->Scene().GetEntityName(h) : StringView(u8"(missing)");
        };

        auto editor =
            MakeRef<ResourceRefEditor>(DefaultAllocator(), name, nameOf(currentTarget()), category);
        ResourceRefEditor* raw = editor.Get();
        raw->OnPick = [self, edit, id, type, propName, currentTarget]()
        {
            if (self->Context == nullptr)
            {
                return;
            }
            // Modal, filterable entity TREE (mirrors the asset picker + hierarchy view), replacing
            // the flat menu that was unusable in large scenes. Pre-selects the current target.
            auto dialog = MakeRef<editor::EntityPickerDialog>(DefaultAllocator(), edit->Scene(),
                                                             currentTarget());
            dialog->OnPicked = [edit, id, type, propName](const Guid& target)
            { edit->SetComponentEntityRef(id, type, propName, target); };
            dialog->Show(self->Context);
        };
        AddEditor(raw, [currentTarget, nameOf, raw]() { raw->SetValueText(nameOf(currentTarget())); });
    }

    void SceneInspectorView::BuildScriptAssetPropertyRow(
        StringView category, const foundation::script::ScriptPropertyDesc& property,
        const RefPtr<ScriptPropertyAccess>& access)
    {
        using foundation::script::ScriptPropertyType;
        using foundation::script::ScriptPropertyValue;
        SceneInspectorView* self = this;
        const String assetType = property.assetType.IsEmpty() ? String(u8"") : property.assetType;

        auto currentTarget = [access]() -> Guid { return access->effective().guid; };

        auto editor = MakeRef<ResourceRefEditor>(DefaultAllocator(), property.name.AsView(),
                                                 AssetNameFor(currentTarget()), category);
        ResourceRefEditor* raw = editor.Get();
        if (!property.description.IsEmpty())
        {
            raw->SetTooltip(property.description.AsView());
        }
        raw->OnPick = [self, access, assetType]()
        {
            if (self->Context == nullptr || self->m_editor->Project() == nullptr)
            {
                return;
            }
            Array<String> typeNames;
            // The harvested "AudioClip" maps to the "AudioClipAsset" source type.
            String assetTypeName(assetType.AsView());
            assetTypeName.Append(u8"Asset");
            typeNames.PushBack(Move(assetTypeName));
            auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                DefaultAllocator(), *self->m_editor, Move(typeNames));
            dialog->OnPicked = [access](const Guid& picked)
            {
                ScriptPropertyValue value;
                value.kind = ScriptPropertyType::Asset;
                value.guid = picked;
                access->setOverride(value);
            };
            dialog->Show(self->Context);
        };
        AddEditor(raw, [self, currentTarget, raw]()
                  { raw->SetValueText(self->AssetNameFor(currentTarget())); });
    }

    void SceneInspectorView::BuildSceneScriptPropertyRows(const TypeInfo* settingsType,
                                                          StringView category)
    {
        using engine::script::SceneScriptSettings;
        using foundation::script::ScriptPropertyValue;
        SceneEditContext* edit = m_edit;
        scene::SceneSystem* system = edit->FindSystemBySettingsType(settingsType);
        if (system == nullptr)
        {
            return;
        }
        auto* live = static_cast<SceneScriptSettings*>(system->SettingsInstance());

        // The bound Level class carries the harvested [metadata] properties. Bind through the
        // editor's resource manager if the Ref has not resolved yet (mirrors BehaviorClass).
        foundation::script::ScriptClass* scriptClass = live->script.Get();
        if (scriptClass == nullptr && !live->script.id.IsNil() && m_editor->Resources() != nullptr)
        {
            scriptClass =
                m_editor->Resources()->Bind<foundation::script::ScriptClass>(live->script.id).Get();
        }
        if (scriptClass == nullptr)
        {
            return; // no Level bound (or not yet cooked) - nothing to author
        }

        // Commit an edited copy of the settings as a version-wrapped blob (one undo step), the
        // same shape SetSceneSettingsBlockCommand reads back.
        auto commit = [edit, settingsType](SceneScriptSettings copy)
        {
            MemoryStream buffer;
            BinarySerializer writer(buffer, SerializeMode::Write);
            foundation::core::BeginVersionedPayload(writer, *settingsType);
            engine::script::SerializeSceneScriptSettings(writer, copy);
            foundation::core::EndVersionedPayload(writer);
            Array<byte> blob;
            const Span<const byte> bytes = buffer.Bytes();
            blob.Reserve(bytes.Size());
            for (byte b : bytes)
            {
                blob.PushBack(b);
            }
            (void)edit->ApplySceneSettingsBlock(settingsType, Move(blob));
        };

        for (const foundation::script::ScriptPropertyDesc& property : scriptClass->properties)
        {
            const u64 hash = property.hash;
            auto access = MakeRef<ScriptPropertyAccess>(DefaultAllocator());
            // Re-resolve the live settings each call (the settings block is replaced wholesale on
            // every edit, so a captured pointer would dangle).
            access->effective = [edit, settingsType, hash, property]() -> ScriptPropertyValue
            {
                scene::SceneSystem* s = edit->FindSystemBySettingsType(settingsType);
                if (s != nullptr)
                {
                    auto* now = static_cast<SceneScriptSettings*>(s->SettingsInstance());
                    if (const auto* over = now->FindOverride(hash))
                    {
                        return over->value;
                    }
                }
                return property.defaultValue;
            };
            access->setOverride = [edit, settingsType, commit, hash](const ScriptPropertyValue& value)
            {
                scene::SceneSystem* s = edit->FindSystemBySettingsType(settingsType);
                if (s == nullptr)
                {
                    return;
                }
                SceneScriptSettings copy = *static_cast<SceneScriptSettings*>(s->SettingsInstance());
                copy.SetOverride(hash, value);
                commit(Move(copy));
            };
            access->removeOverride = [edit, settingsType, commit, hash]()
            {
                scene::SceneSystem* s = edit->FindSystemBySettingsType(settingsType);
                if (s == nullptr)
                {
                    return;
                }
                SceneScriptSettings copy = *static_cast<SceneScriptSettings*>(s->SettingsInstance());
                copy.RemoveOverride(hash);
                commit(Move(copy));
            };
            BuildScriptPropertyRow(category, property, access);
        }
    }

    StringView SceneInspectorView::AssetNameFor(const Guid& target)
    {
        if (target.IsNil())
        {
            return u8"(none)";
        }
        if (m_editor->Project() != nullptr)
        {
            if (foundation::content::Instance* inst =
                    m_editor->Project()->SourceDb().GetInstance(target))
            {
                return inst->Name();
            }
        }
        return u8"(missing)";
    }

    const Float4* SceneInspectorView::RangeOf(const PropertyInfo& prop)
    {
        const core::Attribute* attr = FindAttribute(prop, u8"range");
        return (attr != nullptr) ? attr->value.TryGet<Float4>() : nullptr;
    }

    void SceneInspectorView::AddEditor(ui::toolkit::PropertyEditor* editor,
                                       Function<void()> refresher)
    {
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(editor));
        ui::toolkit::PropertyEditor* raw = editor;
        m_refreshers.PushBack(Function<void()>{[raw, pull = Move(refresher)]()
                                               {
                                                   if (!raw->IsEditing())
                                                   {
                                                       pull();
                                                   }
                                               }});
    }

    void SceneInspectorView::MutateComponent(const Guid& id, const TypeInfo* type,
                                             const Function<void(const Instance&)>& mutate)
    {
        const scene::EntityHandle e = m_edit->Resolve(id);
        scene::ComponentManagerBase* mgr = m_edit->FindManager(type);
        if (mgr == nullptr || !e.IsAssigned() || !mgr->HasComponent(e))
        {
            return;
        }
        Array<byte> before = m_edit->CopyComponent(id, type); // snapshot A (current)
        if (before.IsEmpty())
        {
            return;
        }
        mutate(mgr->GetComponentInstance(e));                // live -> B
        Array<byte> after = m_edit->CopyComponent(id, type); // snapshot B
        // Restore live to A (non-undoable ReadComponent), then PASTE B - the paste command captures
        // the pre-state (A), so the whole mutation is one undo step, exactly like the typed helpers.
        {
            MemoryStream buffer;
            (void)buffer.Write(before.Data(), before.Size());
            (void)buffer.Seek(0, SeekOrigin::Begin);
            BinarySerializer ar(buffer, SerializeMode::Read);
            String typeId;
            foundation::core::Serialize(ar, "type", typeId);
            mgr->ReadComponent(ar, e);
        }
        if (!after.IsEmpty())
        {
            (void)m_edit->PasteComponent(id, Span<const byte>{after.Data(), after.Size()});
        }
    }

    void SceneInspectorView::BuildContainerRows(const Guid& id, const TypeInfo* type,
                                                const PropertyInfo& prop, StringView category)
    {
        if (prop.type == nullptr || prop.type->container == nullptr)
        {
            return;
        }
        SceneInspectorView* self = this;
        const PropertyInfo* propPtr = &prop;
        using MatRef = foundation::resource::Ref<foundation::materials::Material>;

        // Per-slot display text from the live container: a material Ref shows its asset name / "None";
        // a struct element shows its type label. Recomputed by the refresher to detect changes.
        auto computeNames = [self, id, type, propPtr]() -> Array<String>
        {
            Array<String> names;
            scene::ComponentManagerBase* mgr = self->m_edit->FindManager(type);
            const scene::EntityHandle e = self->m_edit->Resolve(id);
            if (mgr == nullptr || !e.IsAssigned() || !mgr->HasComponent(e))
            {
                return names;
            }
            const Instance comp = mgr->GetComponentInstance(e);
            if (comp.IsEmpty())
            {
                return names;
            }
            const Instance container(propPtr->address(comp), propPtr->type);
            const ContainerInfo& ci = *propPtr->type->container;
            const usize n = ContainerSize(ci, container);
            for (usize i = 0; i < n; ++i)
            {
                const Instance el = ContainerAddressAt(ci, container, i);
                if (el.Pointer() != nullptr && el.Type() == &TypeOf<MatRef>())
                {
                    const Guid target = static_cast<const MatRef*>(el.Pointer())->id;
                    names.PushBack(target.IsNil() ? String(u8"None")
                                                  : String(self->AssetNameFor(target)));
                }
                else if (el.Type() != nullptr)
                {
                    names.PushBack(ContainerElementLabel(el.Type()));
                }
                else
                {
                    names.PushBack(String(u8"(none)"));
                }
            }
            return names;
        };

        const String label =
            PrettifyPropertyName(StringView(reinterpret_cast<const utf8char*>(prop.name)));
        auto listEditor = MakeRef<ContainerListEditor>(DefaultAllocator(), label.AsView(), category);
        ContainerListEditor* rawList = listEditor.Get();
        // "description" property attribute -> the list's hover tooltip (the reflection-consistent way
        // to carry help text, e.g. the mesh material slot-0 / submesh semantics).
        if (const core::Attribute* description = FindAttribute(prop, u8"description"))
        {
            if (const String* text = description->value.TryGet<String>())
            {
                rawList->SetTooltip(text->AsView());
            }
        }
        rawList->slotNames = computeNames();

        // Add a default element (homogeneous). The polymorphic add-by-type menu is the next pass.
        rawList->OnAdd = [self, id, type, propPtr]()
        {
            self->MutateComponent(id, type,
                                  [propPtr](const Instance& comp)
                                  {
                                      const Instance container(propPtr->address(comp), propPtr->type);
                                      const ContainerInfo& ci = *propPtr->type->container;
                                      (void)ContainerEmplaceDefault(ci, container,
                                                                    ContainerSize(ci, container));
                                  });
            self->m_forceRebuild = true;
        };
        rawList->OnRemoveSlot = [self, id, type, propPtr](usize i)
        {
            self->MutateComponent(id, type,
                                  [propPtr, i](const Instance& comp)
                                  {
                                      const Instance container(propPtr->address(comp), propPtr->type);
                                      (void)ContainerRemoveAt(*propPtr->type->container, container, i);
                                  });
            self->m_forceRebuild = true;
        };
        rawList->OnMoveSlot = [self, id, type, propPtr](usize i, bool up)
        {
            self->MutateComponent(id, type,
                                  [propPtr, i, up](const Instance& comp)
                                  {
                                      const Instance container(propPtr->address(comp), propPtr->type);
                                      const ContainerInfo& ci = *propPtr->type->container;
                                      const usize n = ContainerSize(ci, container);
                                      if (up && i > 0)
                                      {
                                          (void)ContainerMoveElement(ci, container, i, i - 1);
                                      }
                                      else if (!up && i + 1 < n)
                                      {
                                          (void)ContainerMoveElement(ci, container, i, i + 1);
                                      }
                                  });
            self->m_forceRebuild = true;
        };
        // Pick opens the type-filtered asset picker for this slot (Material).
        rawList->OnPickSlot = [self, id, type, propPtr](usize i)
        {
            if (self->Context == nullptr || self->m_editor->Project() == nullptr)
            {
                return;
            }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"MaterialAsset"));
            auto dialog = MakeRef<editor::app::AssetPickerDialog>(
                DefaultAllocator(), *self->m_editor, Move(typeNames));
            dialog->OnPicked = [self, id, type, propPtr, i](const Guid& target)
            {
                self->MutateComponent(
                    id, type,
                    [propPtr, i, target](const Instance& comp)
                    {
                        const Instance container(propPtr->address(comp), propPtr->type);
                        const ContainerInfo& ci = *propPtr->type->container;
                        if (i >= ContainerSize(ci, container))
                        {
                            return;
                        }
                        const Instance el = ContainerAddressAt(ci, container, i);
                        if (el.Pointer() != nullptr && el.Type() == &TypeOf<MatRef>())
                        {
                            MatRef* r = static_cast<MatRef*>(el.Pointer());
                            *r = MatRef{};
                            r->SetId(target);
                        }
                    });
                self->m_forceRebuild = true;
            };
            dialog->Show(self->Context);
        };

        // One grid row for the whole property; the refresher recomputes the slot text and forces a
        // rebuild when the list changes (count/content) - e.g. from undo/redo, which Signature() misses.
        AddEditor(rawList,
                  [self, rawList, computeNames]()
                  {
                      const Array<String> names = computeNames();
                      if (names.Size() != rawList->slotNames.Size())
                      {
                          self->m_forceRebuild = true;
                          return;
                      }
                      for (usize k = 0; k < names.Size(); ++k)
                      {
                          if (names[k] != rawList->slotNames[k])
                          {
                              self->m_forceRebuild = true;
                              return;
                          }
                      }
                  });
    }

    Float3 SceneInspectorView::EulerDegrees(Quaternion q)
    {
        f32 yaw = 0, pitch = 0, roll = 0;
        ToYawPitchRoll(q, yaw, pitch, roll);
        return Float3{RadiansToDegrees(pitch), RadiansToDegrees(yaw), RadiansToDegrees(roll)};
    }

    void SceneInspectorView::ShowAddComponentMenu()
    {
        const Guid id = SelectedEntity();
        const scene::EntityHandle e = m_edit->Resolve(id);
        if (!e.IsAssigned() || Context == nullptr)
        {
            return;
        }

        SceneEditContext* edit = m_edit;
        auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());

        // Category submenus with authored display names - not a
        // flat raw-type-name dump. Categories and items sort
        // alphabetically so placement is stable as subsystems register.
        struct Entry
        {
            String label;
            StringView category;
            const TypeInfo* type = nullptr;
        };
        Array<Entry> entries;
        m_edit->Scene().ForEachManager(
            [&](scene::ComponentManagerBase& mgr)
            {
                const TypeInfo* type = mgr.ComponentType();
                if (!IsRegisteredType(type) || mgr.HasComponent(e))
                {
                    return;
                } // skip unreflected
                entries.PushBack(Entry{ComponentDisplayName(type), ComponentCategory(type), type});
            });
        const auto viewLess = [](StringView a, StringView b)
        {
            const usize n = Min(a.Size(), b.Size());
            for (usize k = 0; k < n; ++k)
            {
                if (a[k] != b[k])
                {
                    return static_cast<u8>(a[k]) < static_cast<u8>(b[k]);
                }
            }
            return a.Size() < b.Size();
        };
        for (usize i = 1; i < entries.Size(); ++i) // insertion sort: category, then label
        {
            for (usize j = i; j > 0; --j)
            {
                const bool before =
                    viewLess(entries[j].category, entries[j - 1].category) ||
                    (entries[j].category == entries[j - 1].category &&
                     viewLess(entries[j].label.AsView(), entries[j - 1].label.AsView()));
                if (!before)
                {
                    break;
                }
                Swap(entries[j], entries[j - 1]);
            }
        }
        ui::ContextMenu* section = nullptr;
        StringView sectionName;
        for (const Entry& entry : entries)
        {
            if (section == nullptr || entry.category != sectionName)
            {
                ui::MenuItem* item = menu->AddSubmenu(entry.category);
                section = Cast<ui::ContextMenu>(item->Submenu.Get());
                sectionName = entry.category;
            }
            if (section != nullptr)
            {
                const TypeInfo* type = entry.type;
                section->AddItem(entry.label.AsView(),
                                 [edit, id, type]() { edit->AddComponent(id, type); });
            }
        }
        // (Paste lives on the dedicated Paste Component button now - it confirms before overwriting.)
        const Float2 screenPos = m_addButton->LocalToScreen(Float2{0.0f, 0.0f});
        menu->Show(Context, screenPos.x, screenPos.y);
    }

    void SceneInspectorView::UpdatePasteButton()
    {
        if (!m_pasteButton || m_editor == nullptr)
        {
            return;
        }
        const bool hasComponent = !m_editor->ClipboardData(u8"component").IsEmpty();
        const ui::Visibility want = hasComponent ? ui::Visibility::Visible : ui::Visibility::Gone;
        if (m_pasteButton->Visibility != want)
        {
            m_pasteButton->Visibility = want;
            Invalidate();
        }
    }

    void SceneInspectorView::PasteSelectedComponent()
    {
        const Guid id = SelectedEntity();
        const scene::EntityHandle e = m_edit->Resolve(id);
        if (!e.IsAssigned() || Context == nullptr || m_editor == nullptr)
        {
            return;
        }
        const Span<const byte> clip = m_editor->ClipboardData(u8"component");
        if (clip.IsEmpty())
        {
            return;
        }

        // If the entity already has this component type, pasting OVERWRITES it - confirm first (still
        // undoable). Otherwise paste straight away.
        const StringView typeId = SceneEditContext::PeekComponentTypeId(clip);
        scene::ComponentManagerBase* mgr = m_edit->Scene().FindManagerBySerializationId(typeId);
        if (mgr != nullptr && mgr->HasComponent(e))
        {
            String message(u8"This entity already has a ");
            message += ComponentDisplayName(mgr->ComponentType());
            message += StringView(u8" component. Pasting overwrites it (you can undo). Continue?");
            RefPtr<ui::Dialog> dialog =
                ui::Dialog::Confirm(StringView(u8"Overwrite Component?"), message.AsView());
            SceneInspectorView* self = this;
            dialog->OnClosed.Add(
                [self, id](ui::Dialog*, ui::DialogResult result)
                {
                    if (result == ui::DialogResult::OK && self->m_editor != nullptr)
                    {
                        (void)self->m_edit->PasteComponent(
                            id, self->m_editor->ClipboardData(u8"component"));
                    }
                });
            dialog->Show(Context);
            return;
        }
        (void)m_edit->PasteComponent(id, clip);
    }
}
