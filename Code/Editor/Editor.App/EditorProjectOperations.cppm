// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :mcp_operations partition.
//
// EditorProjectOperations: the editor host's IProjectOperations. The cook rides the cook
// service (requested, then awaited by revision); the import runs the editor's two-phase path
// (the worker prepare and the deferred-write flush on the job service, the cheap main-thread
// fan-out between them, held while a cook reads the databases); the export cooks through the
// cook service and then runs the one export entry point on the job service - the same paths
// the menus take, so the editor stays live while an agent's call waits. Every step is
// re-entered from the host's pump: the first call starts the work, later calls poll it, and a
// step that outlives the timeout answers with an error instead of waiting forever.
module;
#include "Core/Prelude.h"

export module editor.app:mcp_operations;

import foundation.core;
import foundation.content;
import pipeline.core;
import pipeline.importer;
import editor.core;
import editor.mcp;

using namespace foundation::core;

export namespace editor::app
{
    /// What the operations run on: the application's services for the open project. Every
    /// pointer is borrowed for the project's life; the strings are resolved on the main thread.
    struct EditorProjectOperationsSeams
    {
        IAllocator* allocator = nullptr;
        editor::EditorProject* project = nullptr;
        editor::EditorContext* context = nullptr; ///< SceneStreamStager / SceneRefScanner, if wired
        editor::EditorCookService* cook = nullptr;
        editor::EditorJobService* jobs = nullptr;
        pipeline::BuilderRegistry* builders = nullptr;
        String hostToolDir;   ///< the player + sidecars beside the executable (the host template)
        String templatesRoot; ///< the resolved templates root (reads settings: main thread)
        String dataRoot;      ///< the engine data root (the shader cook reads <dataRoot>/Shaders)
        f64 timeoutSeconds = 600.0; ///< a step still running past this answers with an error
        /// After a creation: the editor's effects (a first scene as the default, the browser,
        /// the cook), the ones File > New has.
        Function<void(const pipeline::AssetCreator&, foundation::content::Instance&)> onCreated;
        /// After an import: the editor's effects, the ones an import from the Assets browser has
        /// (the import listeners, a model's prefab among them; the cook of what it made; the
        /// browser).
        Function<void(foundation::content::Instance& primary,
                      const pipeline::ImportOptions* options)>
            onImported;
    };

    class EditorProjectOperations final : public editor::mcp::IProjectOperations
    {
    public:
        explicit EditorProjectOperations(EditorProjectOperationsSeams seams)
            : m_seams(Move(seams))
        {
        }

        [[nodiscard]] editor::mcp::OperationStep<editor::mcp::CookOutcome> Cook(bool force) override;
        [[nodiscard]] editor::mcp::OperationStep<editor::mcp::ImportOutcome>
        Import(const editor::mcp::ImportRequest& request) override;
        [[nodiscard]] editor::mcp::OperationStep<editor::mcp::ExportOutcome>
        Export(const editor::mcp::ExportRequest& request) override;
        /// On the main thread, never while a cook or an export reads the databases (it waits,
        /// as an import's placement does); then the host's effects through onCreated.
        [[nodiscard]] editor::mcp::OperationStep<editor::mcp::CreateOutcome>
        Create(const editor::mcp::CreateRequest& request) override;

    private:
        /// A cook requested and awaited: finished once the service's revision has moved past
        /// the one seen at the request and the service is idle again (a request that arrived
        /// mid-cook is remembered by the service and lands one cook later).
        struct CookWait
        {
            bool active = false;
            u64 sinceRevision = 0;
            u64 startedTicks = 0;
        };
        /// The state a job shares with the lambdas it hands the job service: ref-counted so a
        /// job still running when these operations go away keeps its state alive.
        class ImportShared final : public RefCounted
        {
        public:
            RefPtr<Object> prepared; ///< the worker's payload, alive through the flush
            UniquePtr<Array<pipeline::DeferredImportWrite>> writes;
            i64 prepareMs = 0; ///< worker-written before completion, main-read after it
            i64 flushMs = 0;
            bool jobDone = false; ///< set on the main thread by the job's completion
            Status jobStatus;
        };
        struct ImportJob
        {
            enum class Phase : u8
            {
                Idle,
                Preparing,
                Placing,
                Flushing
            };
            Phase phase = Phase::Idle;
            u64 startedTicks = 0;
            i64 mainMs = 0;
            RefPtr<ImportShared> shared;
            editor::mcp::ImportOutcome outcome;
        };
        class ExportShared final : public RefCounted
        {
        public:
            editor::ExportPreset preset;
            String outRoot;
            HashMap<Guid, Array<byte>> sceneStreams;
            Array<Guid> reachableRoots;
            bool reachableValid = false;
            editor::ExportResult result;
            bool jobDone = false;
            Status jobStatus;
        };
        struct ExportJob
        {
            enum class Phase : u8
            {
                Idle,
                Cooking,
                Running
            };
            Phase phase = Phase::Idle;
            u64 startedTicks = 0;
            CookWait cook;
            RefPtr<ExportShared> shared;
        };

        void BeginCook(CookWait& wait, bool force);
        /// True = finished; false = still cooking; an error once the wait outlives the timeout.
        [[nodiscard]] Result<bool, String> PollCook(CookWait& wait);
        [[nodiscard]] editor::mcp::CookOutcome SummaryOutcome() const;
        [[nodiscard]] bool TimedOut(u64 startedTicks) const;
        void SubmitImportFlush(const editor::mcp::ImportRequest& request);
        void SubmitExportJob();

        EditorProjectOperationsSeams m_seams;
        CookWait m_cook;
        ImportJob m_import;
        ExportJob m_export;
        u64 m_createStarted = 0; ///< when a creation first waited on the cook gate; 0 = none
    };
}
