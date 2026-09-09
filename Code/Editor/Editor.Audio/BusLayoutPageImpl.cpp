// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Audio - the `:bus_layout_page` partition (implementation).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.audio;

import foundation.core;
import foundation.content;
import foundation.runtime.client;
import foundation.audio;
import audio.pipeline;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;
namespace runtime = foundation::runtime;
namespace ui = foundation::ui;

namespace editor
{
    namespace
    {
        using Page = AudioBusLayoutEditorPage*;

        constexpr StringView kFixedNames[4] = {u8"Master", u8"Effects", u8"Music", u8"UI"};

        [[nodiscard]] bool EqualsIgnoreCase(StringView a, StringView b)
        {
            if (a.Size() != b.Size())
            {
                return false;
            }
            for (usize i = 0; i < a.Size(); ++i)
            {
                utf8char ca = a.Data()[i];
                utf8char cb = b.Data()[i];
                if (ca >= u8'A' && ca <= u8'Z')
                {
                    ca = static_cast<utf8char>(ca + (u8'a' - u8'A'));
                }
                if (cb >= u8'A' && cb <= u8'Z')
                {
                    cb = static_cast<utf8char>(cb + (u8'a' - u8'A'));
                }
                if (ca != cb)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsFixedBusName(StringView name)
        {
            for (const StringView& fixed : kFixedNames)
            {
                if (EqualsIgnoreCase(name, fixed))
                {
                    return true;
                }
            }
            return false;
        }

        // The custom-slot index whose name matches, or -1 (fixed names / unknown).
        [[nodiscard]] i32 FindSlotByName(const pipeline::AudioBusLayoutAsset& asset, StringView name)
        {
            if (name.IsEmpty() || IsFixedBusName(name))
            {
                return -1;
            }
            for (usize i = 0; i < pipeline::kAudioCustomBusSlotCount; ++i)
            {
                if (!asset.custom[i].name.IsEmpty() && asset.custom[i].name.AsView() == name)
                {
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        void AddRow(ui::toolkit::PropertyGrid& g, RefPtr<ui::toolkit::PropertyEditor> e)
        {
            g.AddProperty(Move(e));
        }

        void RowFloat(ui::toolkit::PropertyGrid& g, StringView name, f32* field, StringView cat,
                      Page page, f64 mn, f64 mx, f64 step)
        {
            String key(cat);
            key.Append(name);
            AddRow(g, RefPtr<ui::toolkit::PropertyEditor>(
                          MakeRef<ui::toolkit::FloatEditor>(
                              editor::EditorRootAllocator(), name, static_cast<f64>(*field), mn, mx, step, 3,
                              Function<void(f64)>{[field, page, key](f64 v)
                                                  {
                                                      *field = static_cast<f32>(v);
                                                      page->CommitEdit(key.AsView());
                                                  }},
                              cat)
                              .Get()));
        }
        void RowBool(ui::toolkit::PropertyGrid& g, StringView name, bool* field, StringView cat,
                     Page page)
        {
            String key(cat);
            key.Append(name);
            AddRow(g, RefPtr<ui::toolkit::PropertyEditor>(
                          MakeRef<ui::toolkit::BoolEditor>(
                              editor::EditorRootAllocator(), name, *field,
                              Function<void(bool)>{[field, page, key](bool v)
                                                   {
                                                       *field = v;
                                                       page->CommitEdit(key.AsView());
                                                   }},
                              cat)
                              .Get()));
        }

        // Every editable field of one Bus, grouped: Mix, then the effect blocks (0 = off).
        void BusRows(ui::toolkit::PropertyGrid& g, pipeline::AudioBusLayoutAsset::Bus& bus, Page page)
        {
            RowFloat(g, u8"Volume", &bus.volume, u8"Mix", page, 0.0, 2.0, 0.01);
            RowBool(g, u8"Muted", &bus.muted, u8"Mix", page);
            RowFloat(g, u8"Lowpass Hz (0 = off)", &bus.lowpassHz, u8"Filter", page, 0.0, 22000.0,
                     10.0);
            RowFloat(g, u8"Highpass Hz (0 = off)", &bus.highpassHz, u8"Filter", page, 0.0, 22000.0,
                     10.0);
            RowFloat(g, u8"Delay s (0 = off)", &bus.delaySeconds, u8"Delay", page, 0.0, 2.0, 0.01);
            RowFloat(g, u8"Delay Decay", &bus.delayDecay, u8"Delay", page, 0.0, 1.0, 0.01);
            RowFloat(g, u8"Reverb Wet (0 = off)", &bus.reverbWet, u8"Reverb", page, 0.0, 1.0, 0.01);
            RowFloat(g, u8"Reverb Room Size", &bus.reverbRoomSize, u8"Reverb", page, 0.0, 1.0,
                     0.01);
            RowFloat(g, u8"Reverb Damping", &bus.reverbDamping, u8"Reverb", page, 0.0, 1.0, 0.01);
        }
    } // namespace

    bool AudioBusWouldCycle(const pipeline::AudioBusLayoutAsset& asset, i32 slotIndex,
                            StringView newParent)
    {
        if (slotIndex < 0 || static_cast<usize>(slotIndex) >= pipeline::kAudioCustomBusSlotCount)
        {
            return false;
        }
        // Walk the proposed parent chain through the custom slots; hitting `slotIndex` = cycle.
        i32 walk = FindSlotByName(asset, newParent);
        for (usize guard = 0; walk >= 0 && guard < pipeline::kAudioCustomBusSlotCount + 1; ++guard)
        {
            if (walk == slotIndex)
            {
                return true;
            }
            walk = FindSlotByName(asset, asset.custom[static_cast<usize>(walk)].parent.AsView());
        }
        return false;
    }

    // ============================ Tree adapter ==============================================

    i32 BusLayoutTreeAdapter::RootCount() const
    {
        return static_cast<i32>(m_owner->m_roots.Size());
    }
    i32 BusLayoutTreeAdapter::GetChildCount(i32 nodeId) const
    {
        if (nodeId == -1)
        {
            return RootCount();
        }
        if (nodeId < 0 || nodeId >= static_cast<i32>(m_owner->m_nodes.Size()))
        {
            return 0;
        }
        return static_cast<i32>(m_owner->m_nodes[static_cast<usize>(nodeId)].children.Size());
    }
    i32 BusLayoutTreeAdapter::GetChildId(i32 parentId, i32 childIndex) const
    {
        if (parentId == -1)
        {
            return (childIndex >= 0 && childIndex < RootCount())
                       ? m_owner->m_roots[static_cast<usize>(childIndex)]
                       : -1;
        }
        if (parentId < 0 || parentId >= static_cast<i32>(m_owner->m_nodes.Size()))
        {
            return -1;
        }
        const Array<i32>& kids = m_owner->m_nodes[static_cast<usize>(parentId)].children;
        return (childIndex >= 0 && childIndex < static_cast<i32>(kids.Size()))
                   ? kids[static_cast<usize>(childIndex)]
                   : -1;
    }
    i32 BusLayoutTreeAdapter::GetDepth(i32 nodeId) const
    {
        return (nodeId >= 0 && nodeId < static_cast<i32>(m_owner->m_nodes.Size()))
                   ? m_owner->m_nodes[static_cast<usize>(nodeId)].depth
                   : 0;
    }
    bool BusLayoutTreeAdapter::HasChildren(i32 nodeId) const { return GetChildCount(nodeId) > 0; }
    RefPtr<ui::View> BusLayoutTreeAdapter::CreateView(i32)
    {
        auto row = MakeRef<ui::EditableLabel>(editor::EditorRootAllocator());
        row->FontSize.SetValue(Optional<f32>{12.0f});
        row->Ellipsis.SetValue(true);
        row->DoubleClickToEdit.SetValue(false);
        row->SlowClickToEdit.SetValue(false);
        return RefPtr<ui::View>(row.Get());
    }
    void BusLayoutTreeAdapter::BindView(ui::View* view, i32 nodeId, i32 depth, bool)
    {
        if (nodeId < 0 || nodeId >= static_cast<i32>(m_owner->m_nodes.Size()) ||
            m_owner->m_asset.Get() == nullptr)
        {
            return;
        }
        const AudioBusLayoutEditorPage::BusNode& node =
            m_owner->m_nodes[static_cast<usize>(nodeId)];
        auto* row = static_cast<ui::EditableLabel*>(view);
        if (node.fixed)
        {
            row->SetText(kFixedNames[static_cast<usize>(node.fixedIndex)]);
        }
        else
        {
            row->SetText(
                m_owner->m_asset->custom[static_cast<usize>(node.slotIndex)].name.AsView());
        }
        row->TextOffsetX.SetValue(m_owner->m_tree->ContentInset(depth));
    }

    // ============================ Construction ==============================================

    AudioBusLayoutEditorPage::AudioBusLayoutEditorPage(EditorContext& context,
                                                       runtime::IApplicationHost& host,
                                                       foundation::content::Instance& instance)
        : app::UIEditorPage(context.Allocator()),
          m_context(&context), m_title(instance.Name())
    {
        (void)host;
        SetInstanceId(instance.Id());
        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset =
            RefPtr<pipeline::AudioBusLayoutAsset>(Cast<pipeline::AudioBusLayoutAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"bus layout '{}' failed to read - page opens empty",
                               m_title);
        }
        m_undoBaseline = SnapshotAsset();

        // Left: the bus tree + Add Bus.
        m_adapter = MakeUnique<BusLayoutTreeAdapter>(Allocator(), *this);
        m_tree = MakeRef<ui::toolkit::DraggableTreeView>(Allocator());
        m_tree->SetItemHeight(22.0f);
        m_tree->SetDragEnabled(false);
        m_tree->SetAdapter(m_adapter.Get());
        {
            AudioBusLayoutEditorPage* self = this;
            m_tree->InternalTreeView()->OnItemClick.Add(
                [self](ui::TreeView::ItemClickInfo info)
                {
                    if (info.NodeId >= 0 && info.NodeId < static_cast<i32>(self->m_nodes.Size()))
                    {
                        self->m_selectedNode = info.NodeId;
                        ui::UIContext* ctx = self->Ctx();
                        if (ctx != nullptr)
                        {
                            ctx->MutationQueueRef().QueueAction(
                                Function<void()>{[self]() { self->RebuildInspector(); }});
                        }
                        else
                        {
                            self->RebuildInspector();
                        }
                    }
                });
        }
        auto addBus = MakeRef<ui::Button>(Allocator(), StringView(u8"+ Add Bus"));
        {
            AudioBusLayoutEditorPage* self = this;
            addBus->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    self->QueueStructural(
                        u8"add-bus",
                        Function<void()>{
                            [self]()
                            {
                                for (usize i = 0; i < pipeline::kAudioCustomBusSlotCount; ++i)
                                {
                                    if (self->m_asset->custom[i].name.IsEmpty())
                                    {
                                        self->m_asset->custom[i].name = Format(u8"Bus{}", i + 1);
                                        self->m_asset->custom[i].parent = String(u8"Master");
                                        self->m_asset->custom[i].bus =
                                            pipeline::AudioBusLayoutAsset::Bus{};
                                        return;
                                    }
                                }
                                LOG_WARNING(u8"Editor",
                                                     u8"bus layout: all {} custom slots in use",
                                                     pipeline::kAudioCustomBusSlotCount);
                            }});
                });
        }
        auto leftColumn = MakeRef<ui::FlexLayout>(Allocator());
        leftColumn->Direction = ui::Orientation::Vertical;
        leftColumn->Spacing = 4.0f;
        leftColumn->Padding = ui::Thickness{6, 4};
        {
            ui::LayoutStyle grow;
            grow.FlexGrow = 1.0f;
            grow.Width = ui::SizeSpec::Match();
            leftColumn->AddView(m_tree.Get(), grow);
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(26.0f));
            leftColumn->AddView(addBus.Get(), lp);
        }

        // Right: the selected bus's inspector.
        m_grid = MakeRef<ui::toolkit::PropertyGrid>(Allocator());
        m_inspectorTitle = MakeRef<ui::Label>(Allocator());
        m_inspectorTitle->FontSize.SetValue(Optional<f32>{12.0f});
        auto rightColumn = MakeRef<ui::FlexLayout>(Allocator());
        rightColumn->Direction = ui::Orientation::Vertical;
        rightColumn->Spacing = 4.0f;
        rightColumn->Padding = ui::Thickness{6, 4};
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            rightColumn->AddView(m_inspectorTitle.Get(), lp);
            ui::LayoutStyle grow;
            grow.FlexGrow = 1.0f;
            grow.Width = ui::SizeSpec::Match();
            rightColumn->AddView(m_grid.Get(), grow);
        }

        auto split = MakeRef<ui::toolkit::SplitView>(Allocator());
        split->SetSplitRatio(0.34f);
        split->SetPanes(leftColumn.Get(), rightColumn.Get());

        // The page action bar (Save / Undo / Redo / Discard) above the split.
        m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this);
        auto column = MakeRef<ui::FlexLayout>(Allocator());
        column->Direction = ui::Orientation::Vertical;
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            column->AddView(m_toolbar.Get(), lp);
            ui::LayoutStyle grow;
            grow.FlexGrow = 1.0f;
            grow.Width = ui::SizeSpec::Match();
            column->AddView(split.Get(), grow);
        }
        m_content = column;

        RebuildTree();
        RebuildInspector();
    }

    // ============================ Tree / inspector ==========================================

    void AudioBusLayoutEditorPage::RebuildTree()
    {
        m_nodes.Clear();
        m_roots.Clear();
        if (m_asset.Get() == nullptr)
        {
            m_tree->SetAdapter(m_adapter.Get());
            return;
        }

        // Nodes 0..3: the fixed buses (Effects/Music/UI under Master).
        for (i32 f = 0; f < 4; ++f)
        {
            BusNode node;
            node.fixed = true;
            node.fixedIndex = f;
            node.depth = (f == 0) ? 0 : 1;
            m_nodes.PushBack(Move(node));
        }
        m_roots.PushBack(0);
        m_nodes[0].children.PushBack(1);
        m_nodes[0].children.PushBack(2);
        m_nodes[0].children.PushBack(3);

        // Used custom slots. First materialize nodes, then link parents (name references may
        // point at slots that appear later in the bank).
        i32 slotNode[pipeline::kAudioCustomBusSlotCount];
        for (usize i = 0; i < pipeline::kAudioCustomBusSlotCount; ++i)
        {
            slotNode[i] = -1;
            if (m_asset->custom[i].name.IsEmpty())
            {
                continue;
            }
            BusNode node;
            node.fixed = false;
            node.slotIndex = static_cast<i32>(i);
            slotNode[i] = static_cast<i32>(m_nodes.Size());
            m_nodes.PushBack(Move(node));
        }
        for (usize i = 0; i < pipeline::kAudioCustomBusSlotCount; ++i)
        {
            if (slotNode[i] < 0)
            {
                continue;
            }
            const StringView parent = m_asset->custom[i].parent.AsView();
            i32 parentNode = 0; // empty/unknown -> Master (the cook's fallback)
            if (EqualsIgnoreCase(parent, u8"Effects"))
            {
                parentNode = 1;
            }
            else if (EqualsIgnoreCase(parent, u8"Music"))
            {
                parentNode = 2;
            }
            else if (EqualsIgnoreCase(parent, u8"UI"))
            {
                parentNode = 3;
            }
            else
            {
                const i32 slot = FindSlotByName(*m_asset, parent);
                if (slot >= 0 && slotNode[static_cast<usize>(slot)] >= 0 &&
                    slot != static_cast<i32>(i))
                {
                    parentNode = slotNode[static_cast<usize>(slot)];
                }
            }
            m_nodes[static_cast<usize>(parentNode)].children.PushBack(slotNode[i]);
        }
        // Depths: BFS from Master (cycles among slots cannot occur - the dropdown refuses them
        // and the cook rejects them - but cap the walk defensively).
        Array<i32> pending;
        pending.PushBack(0);
        for (usize guard = 0; !pending.IsEmpty() && guard < m_nodes.Size() * 2 + 4; ++guard)
        {
            const i32 node = pending.Back();
            pending.PopBack();
            for (i32 child : m_nodes[static_cast<usize>(node)].children)
            {
                m_nodes[static_cast<usize>(child)].depth =
                    m_nodes[static_cast<usize>(node)].depth + 1;
                pending.PushBack(child);
            }
        }

        if (m_selectedNode >= static_cast<i32>(m_nodes.Size()))
        {
            m_selectedNode = 0;
        }
        m_tree->SetAdapter(m_adapter.Get());
        if (ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter())
        {
            HashSet<i32> expanded;
            for (i32 i = 0; i < static_cast<i32>(m_nodes.Size()); ++i)
            {
                expanded.Insert(i);
            }
            flat->SetExpandedNodes(expanded);
        }
        m_tree->InternalTreeView()->InternalListView()->NotifyDataChanged();
    }

    pipeline::AudioBusLayoutAsset::Bus* AudioBusLayoutEditorPage::SelectedBus()
    {
        if (m_asset.Get() == nullptr || m_selectedNode < 0 ||
            m_selectedNode >= static_cast<i32>(m_nodes.Size()))
        {
            return nullptr;
        }
        BusNode& node = m_nodes[static_cast<usize>(m_selectedNode)];
        if (node.fixed)
        {
            switch (node.fixedIndex)
            {
            case 1:
                return &m_asset->effects;
            case 2:
                return &m_asset->music;
            case 3:
                return &m_asset->ui;
            default:
                return &m_asset->master;
            }
        }
        return &m_asset->custom[static_cast<usize>(node.slotIndex)].bus;
    }

    void AudioBusLayoutEditorPage::RebuildInspector()
    {
        m_grid->Clear();
        pipeline::AudioBusLayoutAsset::Bus* bus = SelectedBus();
        if (bus == nullptr)
        {
            m_inspectorTitle->SetText(u8"");
            return;
        }
        AudioBusLayoutEditorPage* self = this;
        Page page = this;
        const BusNode& node = m_nodes[static_cast<usize>(m_selectedNode)];

        if (node.fixed)
        {
            m_inspectorTitle->SetText(kFixedNames[static_cast<usize>(node.fixedIndex)]);
        }
        else
        {
            const i32 slotIndex = node.slotIndex;
            pipeline::AudioBusLayoutAsset::CustomBusSlot& slot =
                m_asset->custom[static_cast<usize>(slotIndex)];
            m_inspectorTitle->SetText(slot.name.AsView());

            // Rename (structural: relabels the tree; refuses empty/fixed/duplicate names).
            AddRow(*m_grid,
                   RefPtr<ui::toolkit::PropertyEditor>(
                       MakeRef<ui::toolkit::StringEditor>(
                           Allocator(), u8"Name", slot.name.AsView(),
                           Function<void(StringView)>{
                               [self, slotIndex](StringView v)
                               {
                                   String name(v);
                                   if (name.IsEmpty() || IsFixedBusName(name.AsView()) ||
                                       FindSlotByName(*self->m_asset, name.AsView()) >= 0)
                                   {
                                       return; // invalid/duplicate: keep the old name
                                   }
                                   self->QueueStructural(
                                       u8"bus-rename",
                                       Function<void()>{
                                           [self, slotIndex, name]()
                                           {
                                               pipeline::AudioBusLayoutAsset::CustomBusSlot& s =
                                                   self->m_asset
                                                       ->custom[static_cast<usize>(slotIndex)];
                                               // Children referencing the old name follow it.
                                               const String oldName(s.name.AsView());
                                               s.name = String(name.AsView());
                                               for (usize i = 0;
                                                    i < pipeline::kAudioCustomBusSlotCount; ++i)
                                               {
                                                   if (self->m_asset->custom[i].parent.AsView() ==
                                                       oldName.AsView())
                                                   {
                                                       self->m_asset->custom[i].parent =
                                                           String(name.AsView());
                                                   }
                                               }
                                           }});
                               }},
                           u8"Bus")
                           .Get()));

            // Parent dropdown: fixed buses + other used slots, minus self + cycle-makers.
            Array<String> owned;
            for (const StringView& fixed : kFixedNames)
            {
                owned.PushBack(String(fixed));
            }
            for (usize i = 0; i < pipeline::kAudioCustomBusSlotCount; ++i)
            {
                if (static_cast<i32>(i) == slotIndex || m_asset->custom[i].name.IsEmpty())
                {
                    continue;
                }
                if (AudioBusWouldCycle(*m_asset, slotIndex, m_asset->custom[i].name.AsView()))
                {
                    continue;
                }
                owned.PushBack(String(m_asset->custom[i].name.AsView()));
            }
            Array<StringView> items;
            for (const String& s : owned)
            {
                items.PushBack(s.AsView());
            }
            i32 current = 0;
            for (usize i = 0; i < owned.Size(); ++i)
            {
                if (EqualsIgnoreCase(owned[i].AsView(), slot.parent.AsView()))
                {
                    current = static_cast<i32>(i);
                    break;
                }
            }
            Array<String> ownedCopy;
            for (const String& s : owned)
            {
                ownedCopy.PushBack(String(s.AsView()));
            }
            AddRow(*m_grid,
                   RefPtr<ui::toolkit::PropertyEditor>(
                       MakeRef<ui::toolkit::EnumEditor>(
                           Allocator(), u8"Parent", current,
                           Span<const StringView>{items.Data(), items.Size()},
                           Function<void(i32)>{
                               [self, slotIndex, ownedCopy = Move(ownedCopy)](i32 v)
                               {
                                   if (v < 0 || static_cast<usize>(v) >= ownedCopy.Size())
                                   {
                                       return;
                                   }
                                   String parent(ownedCopy[static_cast<usize>(v)].AsView());
                                   self->QueueStructural(
                                       u8"bus-parent",
                                       Function<void()>{
                                           [self, slotIndex, parent]()
                                           {
                                               self->m_asset->custom[static_cast<usize>(slotIndex)]
                                                   .parent = String(parent.AsView());
                                           }});
                               }},
                           u8"Bus")
                           .Get()));

            AddRow(*m_grid,
                   RefPtr<ui::toolkit::PropertyEditor>(
                       MakeRef<ui::toolkit::ButtonEditor>(
                           Allocator(), u8"Remove Bus",
                           Function<void()>{
                               [self, slotIndex]()
                               {
                                   self->QueueStructural(
                                       u8"bus-remove",
                                       Function<void()>{
                                           [self, slotIndex]()
                                           {
                                               pipeline::AudioBusLayoutAsset::CustomBusSlot& s =
                                                   self->m_asset
                                                       ->custom[static_cast<usize>(slotIndex)];
                                               // Orphans re-parent to Master (the cook fallback,
                                               // made explicit).
                                               for (usize i = 0;
                                                    i < pipeline::kAudioCustomBusSlotCount; ++i)
                                               {
                                                   if (self->m_asset->custom[i].parent.AsView() ==
                                                       s.name.AsView())
                                                   {
                                                       self->m_asset->custom[i].parent =
                                                           String(u8"Master");
                                                   }
                                               }
                                               s.name = String{};
                                               s.parent = String{};
                                               s.bus = pipeline::AudioBusLayoutAsset::Bus{};
                                               self->m_selectedNode = 0;
                                           }});
                               }},
                           u8"Bus")
                           .Get()));
        }

        BusRows(*m_grid, *bus, page);
    }

    void AudioBusLayoutEditorPage::QueueStructural(StringView undoKey, Function<void()> mutate)
    {
        AudioBusLayoutEditorPage* self = this;
        String key(undoKey);
        auto run = [self, mutate = Move(mutate), key = Move(key)]() mutable
        {
            mutate();
            Array<byte> after = self->SnapshotAsset();
            (void)self->Commands().Execute(
                UniquePtr<IEditorCommand>(self->Allocator().New<EditBusLayoutCommand>(
                                              *self, key.AsView(), self->m_undoBaseline, after),
                                          self->Allocator()));
            self->m_undoBaseline = Move(after);
            self->RebuildTree();
            self->RebuildInspector();
            self->MarkDirty();
        };
        if (ui::UIContext* ctx = Ctx())
        {
            ctx->MutationQueueRef().QueueAction(Function<void()>{Move(run)});
        }
        else
        {
            run();
        }
    }

    // ============================ Undo / save ===============================================

    Array<byte> AudioBusLayoutEditorPage::SnapshotAsset() const
    {
        Array<byte> blob;
        if (m_asset.Get() == nullptr)
        {
            return blob;
        }
        MemoryStream stream;
        BinarySerializer ar(stream, SerializeMode::Write);
        // The snapshot rides a versioned payload exactly like the envelope, so the same reader
        // accepts it (a scope-less blob would be refused).
        foundation::core::BeginVersionedPayload(ar, pipeline::AudioBusLayoutAsset::StaticType());
        m_asset->Serialize(ar);
        foundation::core::EndVersionedPayload(ar);
        const Span<const byte> bytes = stream.Bytes();
        blob.Reserve(bytes.Size());
        for (byte b : bytes)
        {
            blob.PushBack(b);
        }
        return blob;
    }

    void AudioBusLayoutEditorPage::ApplyAssetBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        Array<byte> current = SnapshotAsset();
        if (current.Size() == blob.Size())
        {
            bool same = true;
            for (usize i = 0; i < blob.Size(); ++i)
            {
                if (current[i] != blob[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
            {
                return;
            }
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        foundation::core::BeginVersionedPayload(ar, pipeline::AudioBusLayoutAsset::StaticType());
        m_asset->Serialize(ar);
        foundation::core::EndVersionedPayload(ar);
        m_undoBaseline = blob;
        AudioBusLayoutEditorPage* self = this;
        if (ui::UIContext* ctx = Ctx())
        {
            ctx->MutationQueueRef().QueueAction(Function<void()>{[self]()
                                                                 {
                                                                     self->RebuildTree();
                                                                     self->RebuildInspector();
                                                                 }});
        }
        else
        {
            RebuildTree();
            RebuildInspector();
        }
        MarkDirty();
    }

    void AudioBusLayoutEditorPage::CommitEdit(StringView mergeKey)
    {
        Array<byte> after = SnapshotAsset();
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            Allocator().New<EditBusLayoutCommand>(*this, mergeKey, m_undoBaseline, after),
            Allocator()));
        m_undoBaseline = Move(after);
        MarkDirty();
    }

    ui::UIContext* AudioBusLayoutEditorPage::Ctx() const
    {
        return (m_tree.Get() != nullptr && m_tree->InternalTreeView() != nullptr)
                   ? m_tree->InternalTreeView()->Context
                   : nullptr;
    }

    Status AudioBusLayoutEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved bus layout '{}'", m_title);
        }
        return saved;
    }

    // ============================ Factory ===================================================

    const TypeInfo* AudioBusLayoutPageFactory::PrimaryType() const
    {
        return &pipeline::AudioBusLayoutAsset::StaticType();
    }

    UniquePtr<EditorPage>
    AudioBusLayoutPageFactory::CreatePage(EditorContext& context,
                                          foundation::content::Instance& instance)
    {
        auto* page = editor::EditorRootAllocator().New<AudioBusLayoutEditorPage>(context, *m_host, instance);
        return UniquePtr<EditorPage>(page, editor::EditorRootAllocator());
    }

    void RegisterBusLayoutEditor(EditorContext& context, runtime::IApplicationHost& host)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            editor::EditorRootAllocator().New<AudioBusLayoutPageFactory>(host), editor::EditorRootAllocator()));
    }
}
