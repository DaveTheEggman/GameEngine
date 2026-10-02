// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :resource_ref_editor partition (implementation): the slot it builds, and the one
// assignment BindAsset routes pick, drop and clear through.
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
{
    ResourceRefEditor::ResourceRefEditor(StringView name, StringView valueText,
                                         StringView category,
                                         Span<const StringView> acceptedTypes)
        : ui::toolkit::PropertyEditor(name, category), m_valueText(valueText)
    {
        for (StringView type : acceptedTypes)
        {
            m_acceptedTypes.PushBack(String(type));
        }
        if (!acceptedTypes.IsEmpty() && acceptedTypes[0] != AssetPickerSlot::AnyAssetType() &&
            acceptedTypes[0] != AssetPickerSlot::EntityType())
        {
            m_previewIcon = EditorIcons::Get().ForAssetType(acceptedTypes[0]);
        }
    }

    void ResourceRefEditor::BindAsset(editor::EditorContext& context, Function<Guid()> current,
                                      Function<void(const Guid&)> assign)
    {
        BindAsset(context, Move(current), Move(assign), BindOptions{});
    }

    void ResourceRefEditor::BindAsset(editor::EditorContext& context, Function<Guid()> current,
                                      Function<void(const Guid&)> assign, BindOptions options)
    {
        m_context = &context;
        m_current = Move(current);
        m_assign = Move(assign);
        m_nameFor = Function<String(const Guid&)>{};
        ResourceRefEditor* self = this;
        OnPick = [self]()
        {
            ui::UIContext* ui = self->m_slot.Get() != nullptr ? self->m_slot->Context : nullptr;
            if (ui == nullptr || self->m_context->Project() == nullptr)
            {
                return;
            }
            // The browser-mirroring picker, filtered by the row's types (the wildcard is no
            // filter: an untyped row offers every asset).
            Array<String> names;
            for (const String& type : self->m_acceptedTypes)
            {
                if (type.AsView() != AssetPickerSlot::AnyAssetType())
                {
                    names.PushBack(type);
                }
            }
            auto dialog =
                MakeRef<AssetPickerDialog>(self->MemoryAllocator(), *self->m_context, Move(names));
            dialog->OnPicked = [self](const Guid& picked) { self->Assign(picked); };
            dialog->Show(ui);
        };
        OnAssignDropped = [self](const Guid& id) { self->Assign(id); };
        OnClear = [self]() { self->Assign(Guid{}); };
        OnEdit = Function<void()>{};
        OnReveal = Function<void()>{};
        if (options.edit)
        {
            OnEdit = [self]()
            {
                const Guid id = self->m_current();
                if (!id.IsNil() && self->m_context->OpenAsset)
                {
                    self->m_context->OpenAsset(id);
                }
            };
        }
        if (options.reveal)
        {
            OnReveal = [self]()
            {
                const Guid id = self->m_current();
                if (!id.IsNil() && self->m_context->RevealAsset)
                {
                    self->m_context->RevealAsset(id);
                }
            };
        }
        OnRejectedDrop = [self](StringView assetName, StringView typeName)
        {
            self->m_context->Notify(
                editor::NoticeKind::Warning,
                AssetPickerSlot::RejectionText(assetName, typeName, self->AcceptedTypes(),
                                               u8"field")
                    .AsView());
        };
        Refresh();
    }

    void ResourceRefEditor::BindEntity(Function<Guid()> current,
                                       Function<void(const Guid&)> assign,
                                       Function<String(const Guid&)> nameFor,
                                       Function<void()> pick)
    {
        m_context = nullptr;
        m_current = Move(current);
        m_assign = Move(assign);
        m_nameFor = Move(nameFor);
        OnPick = Move(pick);
        OnEdit = Function<void()>{};
        OnReveal = Function<void()>{};
        OnRejectedDrop = Function<void(StringView, StringView)>{};
        ResourceRefEditor* self = this;
        OnAssignDropped = [self](const Guid& id) { self->Assign(id); };
        OnClear = [self]() { self->Assign(Guid{}); };
        Refresh();
    }

    void ResourceRefEditor::Refresh()
    {
        if (!m_current || (m_context == nullptr && !m_nameFor))
        {
            return;
        }
        const Guid id = m_current();
        m_boundHasValue = !id.IsNil();
        if (id.IsNil())
        {
            SetValueText(m_emptyText.AsView());
        }
        else if (m_nameFor)
        {
            SetValueText(m_nameFor(id).AsView());
        }
        else
        {
            SetValueText(m_context->AssetNameFor(id));
        }
        if (m_slot.Get() != nullptr)
        {
            m_slot->SetValue(m_valueText.AsView(), HasValue());
        }
        if (m_context != nullptr)
        {
            SetPreviewThumbnail((!id.IsNil() && m_context->Thumbnails() != nullptr)
                                    ? m_context->Thumbnails()->Get(id)
                                    : RefPtr<ui::Drawable>{});
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

    void ResourceRefEditor::Assign(const Guid& id)
    {
        if (!m_assign)
        {
            return;
        }
        // The write may rebuild the grid that holds this row (a headless rebuild is
        // synchronous): the row outlives its own assignment.
        RefPtr<ResourceRefEditor> keepAlive(this);
        m_assign(id);
        Refresh();
    }

    RefPtr<ui::View> ResourceRefEditor::CreateEditorView()
    {
        m_slot = MakeRef<AssetPickerSlot>(MemoryAllocator());
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
        Refresh();
        return RefPtr<ui::View>(m_slot.Get());
    }

    RTTI_DEFINE_OBJECT(ResourceRefEditor, "rtti::editor::app")
}
