// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Mcp - :operations partition
//
// IProjectOperations: how a HOST runs the work behind asset_cook / asset_import / project_export.
// The tools themselves are shared - their arguments, refusals and result shapes are the same on
// every host - but WHERE the work runs is the host's: the stdio host runs it inline on the
// calling thread (every step answers at once), the editor on its own background services (the
// cook service, the job service) with the tool re-entered each pump until they finish, so the
// editor never blocks on an agent's call. A step that is not finished is an EMPTY Optional; a
// refusal or failure is the error text the agent reads.
//
// Every operation is IDEMPOTENT ACROSS RE-ENTRIES: a not-finished tool is called again with
// the same arguments on the host's next pump, so the first call with a request starts the
// work and later calls with the same request poll it. An implementation keeps that state
// itself, and one operation of each kind is in flight at a time (the tool is one call).
module;
#include "Core/Prelude.h"

export module editor.mcp:operations;

import foundation.core;
import pipeline.importer;
import editor.project;

using namespace foundation::core;

export namespace editor::mcp
{
    /// A step of an operation: finished with T, not finished yet (empty), or failed/refused
    /// with the text the agent reads.
    template <typename T>
    using OperationStep = Result<Optional<T>, String>;

    /// What asset_cook reports: the plan's split and the build's counts.
    struct CookOutcome
    {
        usize planned = 0;
        usize cooked = 0;
        usize failed = 0;
        usize orphansSwept = 0;
        usize upToDate = 0;
        usize unbuildable = 0;
    };

    /// asset_import's resolved request: the importer is routed by the tool (by extension), the
    /// group path is the agent's (slash-joined; empty = the root).
    struct ImportRequest
    {
        String source;
        String groupPath;
        pipeline::IFileImporter* importer = nullptr;
    };

    /// What asset_import reports: the created asset's identity and where the time went (the
    /// main-thread share is the number that must stay small in the editor).
    struct ImportOutcome
    {
        Guid guid;
        String name;
        String type;
        String typeNamespace;
        String importer;
        usize deferredWrites = 0;
        i64 prepareMs = 0;
        i64 mainMs = 0;
        i64 flushMs = 0;
    };

    /// project_export's resolved request: the preset the tool resolved by name, the output
    /// root, and whether to re-cook everything first.
    struct ExportRequest
    {
        editor::ExportPreset preset;
        String outRoot;
        bool rebuild = false;
    };

    /// What project_export reports: the one export entry point's result.
    struct ExportOutcome
    {
        editor::ExportResult result;
    };

    class IProjectOperations
    {
    public:
        virtual ~IProjectOperations() = default;
        /// The incremental cook over the open project (`force` = rebuild all).
        [[nodiscard]] virtual OperationStep<CookOutcome> Cook(bool force) = 0;
        /// One OS file into the open project's source database (does not cook).
        [[nodiscard]] virtual OperationStep<ImportOutcome> Import(const ImportRequest& request) = 0;
        /// A shippable dist of the open project through the one export entry point.
        [[nodiscard]] virtual OperationStep<ExportOutcome> Export(const ExportRequest& request) = 0;
    };
}
