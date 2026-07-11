// Draconic::EditorCore - :command partition.
//
// The editor undo/redo spine: IEditorCommand + EditorCommandStack. Lumix WorldEditor
// semantics (the best-engineered of the surveyed editors), per docs/design/editor.md §3.4:
//   * every mutation is a command - a failed Execute() means the command is DROPPED, not pushed;
//   * same-type merge against the stack top (a slider drag coalesces into one undo entry);
//   * Begin/EndGroup transactions undo/redo atomically, consecutive same-type groups coalesce
//     unless locked (LockGroup after e.g. "create entity + add component" keeps later identical
//     actions separate).
// Each editor page owns its own stack (see :page); the context routes Edit>Undo/Redo to the
// active page's stack.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.editor.core:command;

import draconic.core;

using namespace draconic::core;

export namespace draconic::editor
{
    // A single reversible edit. TypeId() is the merge/group identity (a stable literal, e.g.
    // "set_property"); commands of different types never merge.
    class IEditorCommand
    {
    public:
        virtual ~IEditorCommand() = default;

        // Apply the edit. Returning false means the edit did nothing (invalid target etc.);
        // the stack discards the command without pushing it.
        [[nodiscard]] virtual bool Execute() = 0;

        // Revert the edit. Only called after a successful Execute().
        virtual void Undo() = 0;

        // Stable identity for merging and group coalescing.
        [[nodiscard]] virtual StringView TypeId() const = 0;

        // Absorb THIS (newer) command into `previous` (already on the stack, same TypeId):
        // typically copy the new target value into `previous`. Return true if absorbed - the
        // stack then re-executes `previous` and discards this command.
        [[nodiscard]] virtual bool MergeInto(IEditorCommand& previous)
        {
            (void)previous;
            return false;
        }
    };

    namespace detail
    {
        inline constexpr StringView kBeginGroupTypeId = u8"__begin_group";
        inline constexpr StringView kEndGroupTypeId   = u8"__end_group";

        // Group brackets: inert markers on the stack; Undo/Redo unwind between them atomically.
        class BeginGroupCommand final : public IEditorCommand
        {
        public:
            explicit BeginGroupCommand(StringView groupType) : m_groupType(groupType) {}
            [[nodiscard]] bool Execute() override { return true; }
            void Undo() override {}
            [[nodiscard]] StringView TypeId() const override { return kBeginGroupTypeId; }
            [[nodiscard]] StringView GroupType() const { return m_groupType.AsView(); }

        private:
            String m_groupType;
        };

        class EndGroupCommand final : public IEditorCommand
        {
        public:
            explicit EndGroupCommand(StringView groupType) : m_groupType(groupType) {}
            [[nodiscard]] bool Execute() override { return true; }
            void Undo() override {}
            [[nodiscard]] StringView TypeId() const override { return kEndGroupTypeId; }
            [[nodiscard]] StringView GroupType() const { return m_groupType.AsView(); }

            bool locked = false;   // a locked group never coalesces with the next same-type group

        private:
            String m_groupType;
        };
    }

    // Linear undo stack with an index; redoing re-executes, any new command truncates the redo
    // tail. Not thread-safe (editor main thread only).
    class EditorCommandStack
    {
    public:
        EditorCommandStack() = default;
        EditorCommandStack(const EditorCommandStack&) = delete;
        EditorCommandStack& operator=(const EditorCommandStack&) = delete;

        /// Fired after any change (execute/undo/redo/clear) - dirty tracking / UI refresh hook.
        Function<void()> OnChanged;

        // Execute `command` and push it. Returns false (command destroyed, stack untouched)
        // if Execute() failed. May merge into the current top instead of pushing.
        bool Execute(UniquePtr<IEditorCommand> command)
        {
            if (!command) { return false; }

            // Same-type merge against the undo top (group markers never match a real TypeId).
            if (m_undoIndex >= 0)
            {
                IEditorCommand& top = *m_stack[static_cast<usize>(m_undoIndex)];
                if (top.TypeId() == command->TypeId() && command->MergeInto(top))
                {
                    const bool ok = top.Execute();
                    DRACONIC_ASSERT(ok);   // re-executing a merged command must not fail
                    (void)ok;
                    Notify();
                    return true;
                }
            }

            if (!command->Execute()) { return false; }   // dropped, not pushed

            TruncateRedo();
            m_stack.PushBack(Move(command));
            ++m_undoIndex;
            Notify();
            return true;
        }

        [[nodiscard]] bool CanUndo() const noexcept { return !m_inGroup && m_undoIndex >= 0; }
        [[nodiscard]] bool CanRedo() const noexcept
        {
            return !m_inGroup && m_undoIndex + 1 < static_cast<i64>(m_stack.Size());
        }

        void Undo()
        {
            if (!CanUndo()) { return; }

            i64 i = m_undoIndex;
            if (m_stack[static_cast<usize>(i)]->TypeId() == detail::kEndGroupTypeId)
            {
                // Unwind the whole group: undo every real command back to the begin marker.
                --i;
                while (i >= 0 && m_stack[static_cast<usize>(i)]->TypeId() != detail::kBeginGroupTypeId)
                {
                    m_stack[static_cast<usize>(i)]->Undo();
                    --i;
                }
                DRACONIC_ASSERT(i >= 0);   // unbalanced group markers
                m_undoIndex = i - 1;       // step past the begin marker
            }
            else
            {
                m_stack[static_cast<usize>(i)]->Undo();
                m_undoIndex = i - 1;
            }
            Notify();
        }

        void Redo()
        {
            if (!CanRedo()) { return; }

            i64 i = m_undoIndex + 1;
            if (m_stack[static_cast<usize>(i)]->TypeId() == detail::kBeginGroupTypeId)
            {
                // Replay the whole group forward to the end marker.
                ++i;
                while (i < static_cast<i64>(m_stack.Size()) &&
                       m_stack[static_cast<usize>(i)]->TypeId() != detail::kEndGroupTypeId)
                {
                    const bool ok = m_stack[static_cast<usize>(i)]->Execute();
                    DRACONIC_ASSERT(ok);   // replaying a previously-successful command must not fail
                    (void)ok;
                    ++i;
                }
                DRACONIC_ASSERT(i < static_cast<i64>(m_stack.Size()));   // unbalanced group markers
                m_undoIndex = i;           // lands on the end marker
            }
            else
            {
                const bool ok = m_stack[static_cast<usize>(i)]->Execute();
                DRACONIC_ASSERT(ok);
                (void)ok;
                m_undoIndex = i;
            }
            Notify();
        }

        // Open a transaction: commands executed until EndGroup() undo/redo as one unit.
        // Consecutive groups of the same type coalesce (the previous group is reopened) unless
        // the previous group was locked. Nesting is not supported.
        void BeginGroup(StringView groupType)
        {
            DRACONIC_ASSERT(!m_inGroup);   // no nested groups
            TruncateRedo();

            // Coalesce: if the undo top is an unlocked end marker of the same group type, pop it
            // so new commands append inside that group (its begin marker stays).
            if (m_undoIndex >= 0)
            {
                IEditorCommand& top = *m_stack[static_cast<usize>(m_undoIndex)];
                if (top.TypeId() == detail::kEndGroupTypeId)   // no RTTI: marker identity is its TypeId
                {
                    auto& end = static_cast<detail::EndGroupCommand&>(top);
                    if (!end.locked && end.GroupType() == groupType)
                    {
                        m_stack.PopBack();
                        --m_undoIndex;
                        m_inGroup = true;
                        m_groupType = String(groupType);
                        return;
                    }
                }
            }

            m_stack.PushBack(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<detail::BeginGroupCommand>(groupType), DefaultAllocator()));
            ++m_undoIndex;
            m_inGroup = true;
            m_groupType = String(groupType);
        }

        void EndGroup()
        {
            DRACONIC_ASSERT(m_inGroup);
            m_stack.PushBack(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<detail::EndGroupCommand>(m_groupType.AsView()), DefaultAllocator()));
            ++m_undoIndex;
            m_inGroup = false;
            Notify();
        }

        // Prevent the most recent group from coalescing with the next same-type group.
        void LockGroup()
        {
            for (i64 i = m_undoIndex; i >= 0; --i)
            {
                IEditorCommand& cmd = *m_stack[static_cast<usize>(i)];
                if (cmd.TypeId() == detail::kEndGroupTypeId)
                {
                    static_cast<detail::EndGroupCommand&>(cmd).locked = true;
                    return;
                }
            }
        }

        void Clear()
        {
            DRACONIC_ASSERT(!m_inGroup);
            m_stack.Clear();
            m_undoIndex = -1;
            Notify();
        }

        // Entry count including group markers (diagnostic / tests).
        [[nodiscard]] usize Size() const noexcept { return m_stack.Size(); }
        [[nodiscard]] i64 UndoIndex() const noexcept { return m_undoIndex; }

    private:
        void TruncateRedo()
        {
            while (static_cast<i64>(m_stack.Size()) > m_undoIndex + 1) { m_stack.PopBack(); }
        }

        void Notify()
        {
            if (OnChanged) { OnChanged(); }
        }

        Array<UniquePtr<IEditorCommand>> m_stack;
        i64 m_undoIndex = -1;   // index of the last executed (undoable) entry
        bool m_inGroup = false;
        String m_groupType;
    };
}
