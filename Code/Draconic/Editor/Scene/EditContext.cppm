// Draconic::EditorScene - :edit partition.
//
// SceneEditContext: the per-page scene mutation mediator (design doc §3.4/§3.6 - the Lumix
// WorldEditor role, but PER PAGE, never global: multi-scene). Every mutation is an
// IEditorCommand on the page's stack; nothing edits the scene directly. Commands reference
// entities by persistent Guid (stable across destroy/undo - handles are generation-guarded
// pool slots and die with the entity), resolving through the scene at execute time.
//
// DestroyEntity's undo restores the FULL serialized subtree - names, transforms, active flags,
// hierarchy, and every serializable component (via the managers' WriteComponent/ReadComponent,
// the same per-entity routing SerializeScene uses). This is the fix for Sedulous's lossy
// "TODO: Restore components and children".
//
// Reparents preserve the WORLD transform (Scene keep-world overloads + TRS decompose); undo
// restores the captured local exactly (no decompose round-trip drift). Same-parent reorders
// keep the local untouched.

module;
#include "Core/Prelude.h"

export module draconic.editor.scene:edit;

import draconic.core;
import draconic.scene;
import draconic.editor.core;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace dscene = draconic::scene;

    class SceneEditContext
    {
    public:
        SceneEditContext(dscene::Scene& scene, EditorCommandStack& commands)
            : m_scene(&scene), m_commands(&commands) {}

        SceneEditContext(const SceneEditContext&) = delete;
        SceneEditContext& operator=(const SceneEditContext&) = delete;

        [[nodiscard]] dscene::Scene& Scene() noexcept { return *m_scene; }
        [[nodiscard]] EditorCommandStack& Commands() noexcept { return *m_commands; }

        /// Per-page entity selection, by persistent Guid (survives destroy/undo round-trips).
        [[nodiscard]] Selection<Guid>& EntitySelection() noexcept { return m_selection; }

        /// Resolve a selected/stored Guid to a live handle (Invalid if the entity is gone).
        [[nodiscard]] dscene::EntityHandle Resolve(const Guid& id) { return m_scene->FindEntity(id); }

        // === Mutations (each an undoable command; failed executes are dropped by the stack) ===

        /// Create an empty entity (child of `parent` when valid). Returns its Guid (empty on
        /// failure). Redo recreates the SAME Guid. The new entity becomes the selection.
        Guid CreateEntity(StringView name, const Guid& parent = Guid{})
        {
            CreateEntityCommand* raw = DefaultAllocator().New<CreateEntityCommand>(*this, name, parent);
            if (!m_commands->Execute(UniquePtr<IEditorCommand>(raw, DefaultAllocator())))
            {
                return Guid{};   // raw was destroyed by the failed Execute
            }
            const Guid id = raw->CreatedId();
            m_selection.Set(id);
            return id;
        }

        /// Destroy an entity and its subtree (undo restores everything, components included).
        void DestroyEntity(const Guid& entity)
        {
            if (!Resolve(entity).IsAssigned()) { return; }
            // Deselect the whole doomed subtree up front (selection is not undoable).
            m_selection.Remove(entity);
            CollectSubtree(Resolve(entity), [this](dscene::EntityHandle e) {
                m_selection.Remove(m_scene->GetEntityId(e));
            });
            (void)m_commands->Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<DestroyEntityCommand>(*this, entity), DefaultAllocator()));
        }

        /// Rename an entity (consecutive renames of the same entity merge into one undo entry).
        void RenameEntity(const Guid& entity, StringView newName)
        {
            (void)m_commands->Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<RenameEntityCommand>(*this, entity, newName), DefaultAllocator()));
        }

        /// Reparent an entity (`newParent` empty = make root; appended at the END of the new
        /// parent's children). Cycles are refused (the command's Execute fails and drops).
        void ReparentEntity(const Guid& entity, const Guid& newParent)
        {
            (void)m_commands->Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<ReparentEntityCommand>(*this, entity, newParent), DefaultAllocator()));
        }

        /// Sibling reorder: move an entity immediately BEFORE `sibling` (under sibling's
        /// parent), or - when `sibling` is empty - to the END of the root list. Undo restores
        /// the previous parent AND position. Cycles/no-ops are refused.
        void MoveEntityBefore(const Guid& entity, const Guid& sibling)
        {
            (void)m_commands->Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<MoveEntityCommand>(*this, entity, sibling), DefaultAllocator()));
        }

        /// True if `possibleAncestor` is `entity` itself or one of its ancestors.
        [[nodiscard]] bool IsSelfOrAncestor(const Guid& entity, const Guid& possibleAncestor)
        {
            dscene::EntityHandle e = Resolve(entity);
            const dscene::EntityHandle anc = Resolve(possibleAncestor);
            if (!anc.IsAssigned()) { return false; }
            for (; e.IsAssigned(); e = m_scene->GetParent(e))
            {
                if (e == anc) { return true; }
            }
            return false;
        }

    private:
        // Depth-first visit of a live subtree (root included).
        template <typename Fn>
        void CollectSubtree(dscene::EntityHandle root, Fn&& fn)
        {
            if (!root.IsAssigned()) { return; }
            fn(root);
            for (dscene::EntityHandle c = m_scene->GetFirstChild(root); c.IsAssigned();
                 c = m_scene->GetNextSibling(c))
            {
                CollectSubtree(c, fn);
            }
        }

        // === Commands ===

        class CreateEntityCommand final : public IEditorCommand
        {
        public:
            CreateEntityCommand(SceneEditContext& ctx, StringView name, const Guid& parent)
                : m_ctx(&ctx), m_name(name), m_parent(parent) {}

            [[nodiscard]] bool Execute() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                const dscene::EntityHandle parent = m_ctx->Resolve(m_parent);
                if (m_parent != Guid{} && !parent.IsAssigned()) { return false; }   // parent gone

                // First execute records the generated Guid; redo recreates the SAME identity.
                const dscene::EntityHandle entity = (m_id != Guid{})
                    ? scene.CreateEntity(m_id, m_name.AsView())
                    : scene.CreateEntity(m_name.AsView());
                if (!entity.IsAssigned()) { return false; }
                if (m_id == Guid{}) { m_id = scene.GetEntityId(entity); }
                if (parent.IsAssigned()) { scene.SetParent(entity, parent); }
                return true;
            }

            void Undo() override
            {
                m_ctx->Scene().DestroyEntity(m_ctx->Resolve(m_id));
            }

            [[nodiscard]] StringView TypeId() const override { return u8"create_entity"; }
            [[nodiscard]] const Guid& CreatedId() const noexcept { return m_id; }

        private:
            SceneEditContext* m_ctx;
            String m_name;
            Guid m_parent;
            Guid m_id;
        };

        class DestroyEntityCommand final : public IEditorCommand
        {
        public:
            DestroyEntityCommand(SceneEditContext& ctx, const Guid& entity)
                : m_ctx(&ctx), m_entity(entity) {}

            [[nodiscard]] bool Execute() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                const dscene::EntityHandle root = m_ctx->Resolve(m_entity);
                if (!root.IsAssigned()) { return false; }

                // Snapshot the subtree PRE-ORDER (parents before children) so Undo can recreate
                // top-down and parent immediately.
                m_records.Clear();
                m_ctx->CollectSubtree(root, [this, &scene](dscene::EntityHandle e) {
                    EntityRecord record;
                    record.id     = scene.GetEntityId(e);
                    record.parent = scene.GetEntityId(scene.GetParent(e));
                    record.name   = String(scene.GetEntityName(e));
                    record.local  = scene.GetLocalTransform(e);
                    record.active = scene.IsActive(e);
                    scene.ForEachManager([&](dscene::ComponentManagerBase& mgr) {
                        if (!mgr.IsSerializable() || !mgr.HasComponent(e)) { return; }
                        ComponentRecord component;
                        component.typeId = String(mgr.SerializationTypeId());
                        MemoryStream buffer;
                        BinarySerializer ar(buffer, SerializeMode::Write);
                        mgr.WriteComponent(ar, e);
                        const Span<const byte> bytes = buffer.Bytes();
                        component.blob.Reserve(bytes.Size());
                        for (byte b : bytes) { component.blob.PushBack(b); }
                        record.components.PushBack(Move(component));
                    });
                    m_records.PushBack(Move(record));
                });

                scene.DestroyEntity(root);
                return true;
            }

            void Undo() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                // Pre-order records: parent entities are recreated before their children.
                for (const EntityRecord& record : m_records)
                {
                    const dscene::EntityHandle e = scene.CreateEntity(record.id, record.name.AsView());
                    scene.SetLocalTransform(e, record.local);
                    scene.SetActive(e, record.active);
                    const dscene::EntityHandle parent = m_ctx->Resolve(record.parent);
                    if (parent.IsAssigned()) { scene.SetParent(e, parent); }
                    for (const ComponentRecord& component : record.components)
                    {
                        dscene::ComponentManagerBase* mgr =
                            scene.FindManagerBySerializationId(component.typeId.AsView());
                        if (mgr == nullptr) { continue; }
                        MemoryStream buffer;
                        (void)buffer.Write(component.blob.Data(), component.blob.Size());
                        (void)buffer.Seek(0, SeekOrigin::Begin);
                        BinarySerializer ar(buffer, SerializeMode::Read);
                        mgr->ReadComponent(ar, e);
                    }
                }
            }

            [[nodiscard]] StringView TypeId() const override { return u8"destroy_entity"; }

        private:
            struct ComponentRecord
            {
                String typeId;
                Array<byte> blob;
            };
            struct EntityRecord
            {
                Guid id;
                Guid parent;   // empty = root (or a parent OUTSIDE the subtree resolved at undo)
                String name;
                Transform local;
                bool active = true;
                Array<ComponentRecord> components;
            };

            SceneEditContext* m_ctx;
            Guid m_entity;
            Array<EntityRecord> m_records;
        };

        class RenameEntityCommand final : public IEditorCommand
        {
        public:
            RenameEntityCommand(SceneEditContext& ctx, const Guid& entity, StringView newName)
                : m_ctx(&ctx), m_entity(entity), m_newName(newName) {}

            [[nodiscard]] bool Execute() override
            {
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (!e.IsAssigned()) { return false; }
                if (!m_hasOld)
                {
                    m_oldName = String(m_ctx->Scene().GetEntityName(e));
                    m_hasOld = true;
                }
                m_ctx->Scene().SetEntityName(e, m_newName.AsView());
                return true;
            }

            void Undo() override
            {
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (e.IsAssigned()) { m_ctx->Scene().SetEntityName(e, m_oldName.AsView()); }
            }

            [[nodiscard]] StringView TypeId() const override { return u8"rename_entity"; }

            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<RenameEntityCommand&>(previous);
                if (prev.m_entity != m_entity) { return false; }
                prev.m_newName = Move(m_newName);   // previous keeps its ORIGINAL old name
                return true;
            }

        private:
            SceneEditContext* m_ctx;
            Guid m_entity;
            String m_newName;
            String m_oldName;
            bool m_hasOld = false;
        };

        class ReparentEntityCommand final : public IEditorCommand
        {
        public:
            ReparentEntityCommand(SceneEditContext& ctx, const Guid& entity, const Guid& newParent)
                : m_ctx(&ctx), m_entity(entity), m_newParent(newParent) {}

            [[nodiscard]] bool Execute() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (!e.IsAssigned()) { return false; }
                const dscene::EntityHandle parent = m_ctx->Resolve(m_newParent);
                if (m_newParent != Guid{} && !parent.IsAssigned()) { return false; }
                // Refuse cycles (reparenting onto self or a descendant).
                if (m_newParent != Guid{} && m_ctx->IsSelfOrAncestor(m_newParent, m_entity)) { return false; }

                if (!m_hasOld)
                {
                    m_oldParent = scene.GetEntityId(scene.GetParent(e));
                    m_oldLocal = scene.GetLocalTransform(e);
                    m_hasOld = true;
                }
                if (m_oldParent == m_newParent) { return false; }   // no-op - don't pollute undo
                // Editor semantics: the entity STAYS PUT in the world across a reparent (its
                // local transform is recomputed via TRS decompose).
                scene.SetParent(e, parent, /*keepWorldTransform*/ true);
                return true;
            }

            void Undo() override
            {
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (!e.IsAssigned()) { return; }
                m_ctx->Scene().SetParent(e, m_ctx->Resolve(m_oldParent));
                m_ctx->Scene().SetLocalTransform(e, m_oldLocal);   // exact, no decompose drift
            }

            [[nodiscard]] StringView TypeId() const override { return u8"reparent_entity"; }

        private:
            SceneEditContext* m_ctx;
            Guid m_entity;
            Guid m_newParent;
            Guid m_oldParent;
            Transform m_oldLocal;
            bool m_hasOld = false;
        };

        // Sibling reorder: place `entity` before `sibling` (empty sibling = end of the ROOT
        // list). Undo restores the exact previous position via the captured old next-sibling
        // (empty = was last under its old parent).
        class MoveEntityCommand final : public IEditorCommand
        {
        public:
            MoveEntityCommand(SceneEditContext& ctx, const Guid& entity, const Guid& sibling)
                : m_ctx(&ctx), m_entity(entity), m_sibling(sibling) {}

            [[nodiscard]] bool Execute() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (!e.IsAssigned()) { return false; }
                const dscene::EntityHandle sibling = m_ctx->Resolve(m_sibling);
                if (m_sibling != Guid{} && !sibling.IsAssigned()) { return false; }
                if (m_sibling == m_entity) { return false; }
                // Cycle: the target slot's PARENT lies inside the moved entity's own subtree.
                if (sibling.IsAssigned())
                {
                    const dscene::EntityHandle parent = scene.GetParent(sibling);
                    if (parent.IsAssigned()
                        && m_ctx->IsSelfOrAncestor(scene.GetEntityId(parent), m_entity))
                    {
                        return false;
                    }
                }

                if (!m_hasOld)
                {
                    m_oldParent = scene.GetEntityId(scene.GetParent(e));
                    m_oldNext = scene.GetEntityId(scene.GetNextSibling(e));
                    m_oldLocal = scene.GetLocalTransform(e);
                    m_hasOld = true;
                }

                // Keep the world transform only when the PARENT changes; a same-parent reorder
                // keeps the exact local (no decompose round-trip noise).
                const dscene::EntityHandle newParent = sibling.IsAssigned()
                    ? scene.GetParent(sibling) : dscene::EntityHandle::Invalid();
                const bool parentChanges = scene.GetEntityId(newParent) != m_oldParent;

                const u64 before = scene.Revision();
                if (sibling.IsAssigned()) { scene.MoveBefore(e, sibling, parentChanges); }
                else { scene.SetParent(e, dscene::EntityHandle::Invalid(), parentChanges); }
                return scene.Revision() != before;   // unchanged position = no-op, drop
            }

            void Undo() override
            {
                dscene::Scene& scene = m_ctx->Scene();
                const dscene::EntityHandle e = m_ctx->Resolve(m_entity);
                if (!e.IsAssigned()) { return; }
                const dscene::EntityHandle oldNext = m_ctx->Resolve(m_oldNext);
                if (oldNext.IsAssigned()) { scene.MoveBefore(e, oldNext); }
                else { scene.SetParent(e, m_ctx->Resolve(m_oldParent)); }   // was last: append
                scene.SetLocalTransform(e, m_oldLocal);   // exact, no decompose drift
            }

            [[nodiscard]] StringView TypeId() const override { return u8"move_entity"; }

        private:
            SceneEditContext* m_ctx;
            Guid m_entity;
            Guid m_sibling;
            Guid m_oldParent;
            Guid m_oldNext;
            Transform m_oldLocal;
            bool m_hasOld = false;
        };

        dscene::Scene* m_scene;              // borrowed (SceneSubsystem owns it via the page)
        EditorCommandStack* m_commands;      // borrowed (the page owns its stack)
        Selection<Guid> m_selection;
    };
}
