# Export - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/export.md (+ export-templates.md, export-reachability.md)
> Track: [[settings-and-export-track]]

NON-AUTHORITATIVE. The problem framing + the design that produced the export system. Present-tense truth
is the three export Systems docs; the full original doc is in git at the P0 commit 3b92560d. Kept for
the "why".

## The problem

Export a project to a runnable dist (content pak + player.xml manifest) PLUS a player executable + its
runtime files, per-platform, multiple at once, uniform across CLI + editor (one driver, like cook), with
export configs authored in the editor and importable Godot-style templates. The CLI's hardcoded
`argv[0]/../../Player/DraconicPlayer` walk broke on the `Bin/<Config>/<Platform>-<Compiler>` layout
change and the editor never did it at all - the trigger for the redesign.

## The one decision everything hangs on

Separate the portable prebuilt bundle (`ExportTemplate`, machine-local, referenced by id) from the
project-local config (`ExportPreset`, committed, no machine paths). Templates live in a templates root
outside any project; a host implicit template is synthesized from the running tool's dir so dev export
works with zero setup. This is the Godot export-templates model.

## How it landed vs the plan

Everything the design proposed shipped, including the two items handed off mid-track:
- **Template create + the config axis** (`CreateTemplate`, `--template create`, `(platform, config)`
  identity, `FindBy`, categorized `sidecars` + `symbols`) - shipped (was "QUEUED - handed to fable").
- **Reachability pruning** (`pruneToReachable`, `ExportRoots`, closure driving cook + pack, the editor
  main-thread pre-scan) - shipped Phases 1+2 (was "QUEUED").
- **The remaining editor UI** (Manage Templates panel, preset-editor form) - shipped (was "remaining,
  not queued to anyone").

## Precursors

Both shipped ahead of export: `foundation.settings` + Core `GetEnvironmentVariable` /
`GetUserDataDirectory` (the templates-root resolution + per-user settings), and the shell
`IDialogService` (SDL3 file/folder dialogs + `OpenPath` native reveal).

## Config-driven sidecars

The original design listed a hardcoded `KnownSidecars`; it was replaced by
`draconic_copy_runtime_deps` emitting `<player>.runtime-libs` at build time, which the template reads -
the manifest is authoritative, so the sidecar list is never hand-maintained.
